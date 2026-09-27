#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Run read/functional ESP32-S3 HIL checks for board peripherals in the frozen
 * P0 order: QMI8658, AXP2101, then PCF85063A. */
esp_err_t gh_peripherals_run_hil(void);

typedef struct {
    bool battery_present;
    bool percentage_available;
    uint8_t percentage;
    bool voltage_available;
    uint16_t voltage_mv;
    bool charging;
} gh_companion_power_t;

/** Read the live AXP2101 power state without estimating SOC from voltage. */
esp_err_t gh_peripherals_read_power(gh_companion_power_t *power);

#ifdef __cplusplus
}
#endif
