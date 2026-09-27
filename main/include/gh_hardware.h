#pragma once

#include "esp_err.h"

/**
 * Verify and report the real ESP32-S3 hardware identity before the application
 * initializes storage, networking, display, or audio.
 *
 * This is a runtime check. Build-target configuration alone is not accepted as
 * ESP32-S3 Hardware-in-the-loop evidence.
 */
esp_err_t gh_hardware_verify_esp32s3(void);

/**
 * Report the Waveshare board-variant signal exposed by the detected touch
 * controller. The physical PCB revision label remains authoritative.
 */
esp_err_t gh_hardware_report_board_variant(void);
