#include "gh_peripherals.h"

#include <inttypes.h>
#include <stdbool.h>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gh_imu.h"
#include "pcf85063a.h"

static const char *TAG = "gh_peripherals";

#define HIL_I2C_TIMEOUT_MS 100
#define AXP2101_ADDRESS 0x34
#define AXP2101_IC_TYPE_REGISTER 0x03
#define AXP2101_EXPECTED_CHIP_ID 0x4A
#define AXP2101_STATUS1_REGISTER 0x00
#define AXP2101_STATUS2_REGISTER 0x01
#define AXP2101_BAT_VOLTAGE_HIGH_REGISTER 0x34
#define AXP2101_BAT_VOLTAGE_LOW_REGISTER 0x35
#define AXP2101_BAT_PERCENT_REGISTER 0xA4

static i2c_master_dev_handle_t s_axp2101;

static esp_err_t axp2101_read(uint8_t reg, uint8_t *value, size_t length)
{
    if (s_axp2101 == NULL || value == NULL || length == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_axp2101, &reg, 1, value, length,
                                       HIL_I2C_TIMEOUT_MS);
}

static esp_err_t axp2101_hil(i2c_master_bus_handle_t bus)
{
    esp_err_t ret = i2c_master_probe(bus, AXP2101_ADDRESS, HIL_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) return ret;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDRESS,
        .scl_speed_hz = 100000,
    };
    if (s_axp2101 == NULL &&
        (ret = i2c_master_bus_add_device(bus, &config, &s_axp2101)) != ESP_OK) return ret;
    uint8_t reg = AXP2101_IC_TYPE_REGISTER;
    uint8_t chip_id = 0;
    ret = i2c_master_transmit_receive(s_axp2101, &reg, 1, &chip_id, 1,
                                      HIL_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) return ret;
    if (chip_id != AXP2101_EXPECTED_CHIP_ID) return ESP_ERR_INVALID_RESPONSE;
    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 AXP2101 Read | PASS | "
             "address=0x%02X chip_id=0x%02X mode=read-only",
             AXP2101_ADDRESS, chip_id);
    return ESP_OK;
}

esp_err_t gh_peripherals_read_power(gh_companion_power_t *power)
{
    if (power == NULL) return ESP_ERR_INVALID_ARG;
    *power = (gh_companion_power_t){0};
    uint8_t status1 = 0;
    uint8_t status2 = 0;
    ESP_RETURN_ON_ERROR(axp2101_read(AXP2101_STATUS1_REGISTER, &status1, 1),
                        TAG, "AXP2101 status1 read");
    ESP_RETURN_ON_ERROR(axp2101_read(AXP2101_STATUS2_REGISTER, &status2, 1),
                        TAG, "AXP2101 status2 read");
    power->battery_present = (status1 & (1U << 3)) != 0;
    power->charging = (status2 >> 5) == 0x01;
    if (!power->battery_present) return ESP_OK;

    uint8_t voltage[2] = {0};
    if (axp2101_read(AXP2101_BAT_VOLTAGE_HIGH_REGISTER, voltage,
                     sizeof(voltage)) == ESP_OK) {
        const uint16_t raw = (uint16_t)(((voltage[0] & 0x1FU) << 8) | voltage[1]);
        if (raw >= 2500 && raw <= 5000) {
            power->voltage_mv = raw;
            power->voltage_available = true;
        }
    }
    uint8_t percentage = 0;
    if (axp2101_read(AXP2101_BAT_PERCENT_REGISTER, &percentage, 1) == ESP_OK &&
        percentage <= 100) {
        power->percentage = percentage;
        power->percentage_available = true;
    }
    return ESP_OK;
}

static esp_err_t pcf85063_hil(i2c_master_bus_handle_t bus)
{
    esp_err_t ret = i2c_master_probe(bus, PCF85063A_ADDRESS, HIL_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) return ret;
    pcf85063a_dev_t rtc = {0};
    if ((ret = pcf85063a_init(&rtc, bus, PCF85063A_ADDRESS)) != ESP_OK) return ret;
    pcf85063a_datetime_t first = {0};
    pcf85063a_datetime_t second = {0};
    if ((ret = pcf85063a_get_time_date(&rtc, &first)) != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(1100));
    if ((ret = pcf85063a_get_time_date(&rtc, &second)) != ESP_OK) return ret;
    const bool ticking = first.sec != second.sec || first.min != second.min ||
                         first.hour != second.hour || first.day != second.day;
    if (!ticking) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 PCF85063 Read | PASS | "
             "address=0x%02X time=%04u-%02u-%02uT%02u:%02u:%02u ticking=true",
             PCF85063A_ADDRESS, (unsigned)second.year, (unsigned)second.month,
             (unsigned)second.day, (unsigned)second.hour, (unsigned)second.min,
             (unsigned)second.sec);
    return ESP_OK;
}

esp_err_t gh_peripherals_run_hil(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) return ESP_ERR_INVALID_STATE;

    /* gh_imu owns the single QMI8658 driver instance and reuses the BSP I2C
     * bus for both boot HIL and the later runtime sampling task. */
    esp_err_t ret = gh_imu_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP32-S3 QMI8658 HIL failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = axp2101_hil(bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP32-S3 AXP2101 HIL failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = pcf85063_hil(bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP32-S3 PCF85063 HIL failed: %s", esp_err_to_name(ret));
    }
    return ret;
}
