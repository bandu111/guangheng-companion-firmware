#pragma once

#include "esp_err.h"
#include "gh_model.h"

typedef struct {
    const char *base_url;
    const char *device_id;
    bool allow_insecure_http;
} gh_backend_config_t;

typedef struct {
    char transcript[160];
    char answer[320];
    char intent[40];
    bool requires_touch_approval;
} gh_backend_voice_result_t;

esp_err_t gh_backend_init(const gh_backend_config_t *config);
esp_err_t gh_backend_refresh(gh_app_model_t *model);
esp_err_t gh_backend_approve(const gh_app_model_t *model);
esp_err_t gh_backend_voice(const uint8_t *wav_data, size_t wav_size,
                           gh_backend_voice_result_t *voice_result);
bool gh_backend_is_configured(void);
esp_err_t gh_backend_pair_if_needed(gh_app_model_t *model);
