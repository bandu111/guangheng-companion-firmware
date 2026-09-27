#pragma once

#include "esp_err.h"
#include "gh_model.h"

typedef void (*gh_ui_approval_cb_t)(void *context);
typedef void (*gh_ui_voice_cb_t)(void *context);
typedef void (*gh_ui_navigation_cb_t)(gh_screen_t screen, void *context);

esp_err_t gh_ui_start(gh_app_model_t *model,
                      gh_ui_approval_cb_t approval_cb,
                      gh_ui_voice_cb_t voice_cb,
                      gh_ui_navigation_cb_t navigation_cb,
                      void *context);
void gh_ui_render(const gh_app_model_t *model);
void gh_ui_show_ble_passkey(uint32_t passkey);
void gh_ui_hide_ble_passkey(void);
