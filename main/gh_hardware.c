#include "gh_hardware.h"

#include <inttypes.h>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_chip_info.h"
#include "esp_check.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "GuangHeng Energy Companion requires the ESP32-S3 build target"
#endif

static const char *TAG = "gh_hardware";

esp_err_t gh_hardware_verify_esp32s3(void)
{
    esp_chip_info_t chip = {0};
    esp_chip_info(&chip);
    if (chip.model != CHIP_ESP32S3) {
        ESP_LOGE(TAG,
                 "ESP32-S3 Hardware-in-the-loop | ESP32-S3 Detected | FAIL | "
                 "unexpected chip model=%d; initialization stopped",
                 chip.model);
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint32_t flash_size = 0;
    ESP_RETURN_ON_ERROR(esp_flash_get_size(NULL, &flash_size), TAG,
                        "ESP32-S3 flash-size read failed");

    uint8_t mac[6] = {0};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG,
                        "ESP32-S3 MAC read failed");

    const size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 Detected | PASS | "
             "model=ESP32-S3 revision=%d.%d cores=%d",
             chip.revision / 100, chip.revision % 100, chip.cores);
    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 Flash | PASS | "
             "size=%" PRIu32 " bytes (%" PRIu32 " MB)",
             flash_size, flash_size / (1024U * 1024U));
    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 PSRAM | %s | "
             "size=%u bytes (%u MB)",
             psram_size > 0 ? "PASS" : "FAIL", (unsigned)psram_size,
             (unsigned)(psram_size / (1024U * 1024U)));
    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 MAC | PASS | "
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (psram_size == 0) {
        ESP_LOGE(TAG, "ESP32-S3 PSRAM is required; initialization stopped");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t gh_hardware_report_board_variant(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const bool cst816s = i2c_master_probe(
                            bus, ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS, 100) == ESP_OK;
    const bool ft5x06 = i2c_master_probe(
                           bus, ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS, 100) == ESP_OK;

    if (cst816s == ft5x06) {
        ESP_LOGW(TAG,
                 "ESP32-S3 Hardware-in-the-loop | Board Revision Confirmation | "
                 "UNVERIFIED | CST816S=%s FT5x06=%s; inspect physical PCB label",
                 cst816s ? "present" : "absent", ft5x06 ? "present" : "absent");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | Board Variant Signal | PASS | "
             "touch=%s; physical PCB revision label still required",
             cst816s ? "CST816S (V2-family signal)" : "FT5x06 (original-family signal)");
    return ESP_OK;
}
