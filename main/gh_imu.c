#include "gh_imu.h"

#include <stdbool.h>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qmi8658.h"
#include <math.h>

#define GH_IMU_SAMPLE_PERIOD_MS 40
#define GH_IMU_I2C_TIMEOUT_MS 100
#define GH_IMU_RESET_REGISTER 0x60
#define GH_IMU_RESET_COMMAND 0xB0
#define GH_IMU_CTRL1_VALUE 0x60

static const char *TAG = "gh_imu";
static qmi8658_dev_t s_imu;
static bool s_initialized;
static TaskHandle_t s_task;
static gh_imu_lift_callback_t s_callback;
static void *s_callback_context;
static gh_lift_detector_t s_detector;

static esp_err_t configure_device(void)
{
    esp_err_t err = qmi8658_write_register(&s_imu, GH_IMU_RESET_REGISTER,
                                            GH_IMU_RESET_COMMAND);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));
    if ((err = qmi8658_write_register(&s_imu, QMI8658_CTRL1,
                                      GH_IMU_CTRL1_VALUE)) != ESP_OK ||
        (err = qmi8658_set_accel_range(&s_imu, QMI8658_ACCEL_RANGE_4G)) != ESP_OK ||
        (err = qmi8658_set_accel_odr(&s_imu, QMI8658_ACCEL_ODR_31_25HZ)) != ESP_OK ||
        (err = qmi8658_set_gyro_range(&s_imu, QMI8658_GYRO_RANGE_256DPS)) != ESP_OK ||
        (err = qmi8658_set_gyro_odr(&s_imu, QMI8658_GYRO_ODR_31_25HZ)) != ESP_OK) {
        return err;
    }
    qmi8658_set_accel_unit_mps2(&s_imu, true);
    qmi8658_set_gyro_unit_dps(&s_imu, true);
    return qmi8658_enable_sensors(&s_imu,
                                  QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO);
}

esp_err_t gh_imu_init(void)
{
    if (s_initialized) return ESP_OK;
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) return ESP_ERR_INVALID_STATE;
    const uint8_t candidates[] = {QMI8658_ADDRESS_HIGH, QMI8658_ADDRESS_LOW};
    uint8_t address = 0;
    for (size_t i = 0; i < sizeof(candidates); ++i) {
        if (i2c_master_probe(bus, candidates[i], GH_IMU_I2C_TIMEOUT_MS) == ESP_OK) {
            address = candidates[i];
            break;
        }
    }
    if (address == 0) return ESP_ERR_NOT_FOUND;
    esp_err_t err = qmi8658_init(&s_imu, bus, address);
    if (err != ESP_OK) return err;
    uint8_t who_am_i = 0;
    if ((err = qmi8658_get_who_am_i(&s_imu, &who_am_i)) != ESP_OK ||
        (err = configure_device()) != ESP_OK) return err;
    s_initialized = true;

    gh_imu_sample_t sample = {0};
    for (int attempt = 0; attempt < 12; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(GH_IMU_SAMPLE_PERIOD_MS));
        if (gh_imu_read_sample(&sample) == ESP_OK) break;
    }
    if (!sample.valid) {
        s_initialized = false;
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 QMI8658 Runtime | "
             "PASS | address=0x%02X who_am_i=0x%02X rate=25Hz "
             "accel=%.3f,%.3f,%.3f gyro=%.3f,%.3f,%.3f",
             address, who_am_i, sample.accel_x, sample.accel_y, sample.accel_z,
             sample.gyro_x, sample.gyro_y, sample.gyro_z);
    return ESP_OK;
}

esp_err_t gh_imu_read_sample(gh_imu_sample_t *sample)
{
    if (!s_initialized || sample == NULL) return ESP_ERR_INVALID_STATE;
    bool ready = false;
    esp_err_t err = qmi8658_is_data_ready(&s_imu, &ready);
    if (err != ESP_OK) return err;
    if (!ready) return ESP_ERR_NOT_FINISHED;
    qmi8658_data_t raw = {0};
    if ((err = qmi8658_read_sensor_data(&s_imu, &raw)) != ESP_OK) return err;
    *sample = (gh_imu_sample_t) {
        .accel_x = raw.accelX,
        .accel_y = raw.accelY,
        .accel_z = raw.accelZ,
        .gyro_x = raw.gyroX,
        .gyro_y = raw.gyroY,
        .gyro_z = raw.gyroZ,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .valid = true,
    };
    return ESP_OK;
}

static void imu_task(void *argument)
{
    (void)argument;
    gh_lift_config_t config = gh_lift_default_config();
    gh_lift_detector_init(&s_detector, &config);
    gh_lift_state_t previous = s_detector.state;
    uint32_t consecutive_failures = 0;
    uint32_t successful_samples = 0;
    while (true) {
        gh_imu_sample_t sample = {0};
        esp_err_t err = gh_imu_read_sample(&sample);
        if (err == ESP_OK) {
            consecutive_failures = 0;
            successful_samples++;
            const bool lifted = gh_lift_detector_update(&s_detector, &sample);
            if (s_detector.state != previous) {
                const float accel_mag = sqrtf(s_detector.filtered_ax * s_detector.filtered_ax +
                                              s_detector.filtered_ay * s_detector.filtered_ay +
                                              s_detector.filtered_az * s_detector.filtered_az);
                const float gyro_mag = sqrtf(s_detector.filtered_gx * s_detector.filtered_gx +
                                             s_detector.filtered_gy * s_detector.filtered_gy +
                                             s_detector.filtered_gz * s_detector.filtered_gz);
                ESP_LOGI(TAG,
                         "ESP32-S3 Lift transition %s -> %s accel_mag=%.2f "
                         "gyro_mag=%.2f angle_delta=%.1f",
                         gh_lift_state_name(previous),
                         gh_lift_state_name(s_detector.state), accel_mag, gyro_mag,
                         s_detector.orientation_delta_deg);
                previous = s_detector.state;
            }
            if (successful_samples == 125) {
                ESP_LOGI(TAG,
                         "ESP32-S3 IMU task stack high-water=%lu words after "
                         "125 real samples",
                         (unsigned long)uxTaskGetStackHighWaterMark(NULL));
            }
            if (lifted) {
                ESP_LOGI(TAG,
                         "ESP32-S3 Hardware-in-the-loop | ESP32-S3 Lift Event | "
                         "PASS | count=%lu timestamp_ms=%lu",
                         (unsigned long)s_detector.lift_events,
                         (unsigned long)sample.timestamp_ms);
                if (s_callback != NULL) s_callback(sample.timestamp_ms,
                                                   s_callback_context);
            }
        } else if (err != ESP_ERR_NOT_FINISHED) {
            consecutive_failures++;
            gh_imu_sample_t invalid = {.valid = false};
            gh_lift_detector_update(&s_detector, &invalid);
            if (consecutive_failures == 1 || consecutive_failures % 25 == 0) {
                ESP_LOGW(TAG,
                         "ESP32-S3 QMI8658 runtime read unavailable; lift disabled "
                         "until real samples resume: %s failures=%lu",
                         esp_err_to_name(err), (unsigned long)consecutive_failures);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(GH_IMU_SAMPLE_PERIOD_MS));
    }
}

esp_err_t gh_imu_start(gh_imu_lift_callback_t callback, void *context)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_task != NULL) return ESP_ERR_INVALID_STATE;
    s_callback = callback;
    s_callback_context = context;
    if (xTaskCreate(imu_task, "gh_imu", 4096, NULL, 6, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "ESP32-S3 QMI8658 sampling started rate=25Hz stack=4096 "
             "stationary_ms=720 readable_ms=280 cooldown_ms=4000");
    return ESP_OK;
}

void gh_imu_stop(void)
{
    if (s_task != NULL) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    s_callback = NULL;
    s_callback_context = NULL;
}
