#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define GH_WIFI_SSID_MAX 33
#define GH_WIFI_PASSWORD_MAX 65

esp_err_t gh_ble_provisioning_start(void);
bool gh_ble_provisioning_load_wifi(char *ssid, size_t ssid_size,
                                   char *password, size_t password_size);
bool gh_ble_provisioning_take_pending(char *ssid, size_t ssid_size,
                                      char *password, size_t password_size);
void gh_ble_provisioning_report_wifi(bool connected);
