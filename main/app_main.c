#include "esp_log.h"
#include "nvs_flash.h"

#include "gh_app.h"
#include "gh_hardware.h"

static const char *TAG = "gh_main";

void app_main(void)
{
    ESP_ERROR_CHECK(gh_hardware_verify_esp32s3());

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG,
             "Starting GuangHeng Energy Companion on Waveshare "
             "ESP32-S3-Touch-AMOLED-1.8");
    ESP_ERROR_CHECK(gh_app_start());
}
