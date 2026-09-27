#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    float bx_mt;
    float by_mt;
    float bz_mt;
    float magnitude_mt;
    float temperature_c;
    bool attached;
} gh_hall_sample_t;

esp_err_t gh_hall_init(void);
esp_err_t gh_hall_read(gh_hall_sample_t *sample);
bool gh_hall_is_enabled(void);
