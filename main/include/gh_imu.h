#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "gh_lift_detector.h"

typedef void (*gh_imu_lift_callback_t)(uint32_t timestamp_ms, void *context);

esp_err_t gh_imu_init(void);
esp_err_t gh_imu_read_sample(gh_imu_sample_t *sample);
esp_err_t gh_imu_start(gh_imu_lift_callback_t callback, void *context);
void gh_imu_stop(void);

