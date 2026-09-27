#include "gh_app.h"

#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "sdkconfig.h"
#include <string.h>
#include <time.h>

#include "gh_backend.h"
#include "gh_ble_provisioning.h"
#include "gh_audio.h"
#include "gh_hall.h"
#include "gh_imu.h"
#include "gh_model.h"
#include "gh_peripherals.h"
#include "gh_ui.h"

static const char *TAG = "gh_app";
static gh_app_model_t s_model;
static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT BIT0
static bool s_sntp_started;
static bool s_time_ready_logged;
static bool s_wifi_has_credentials;
static bool s_power_hil_logged;
#define BACKEND_REFRESH_INTERVAL_MS 15000
#define EXPLAIN_RETURN_MS 10000

typedef enum {
    GH_APP_EVENT_LIFT = 1,
    GH_APP_EVENT_EXPLAIN_TIMEOUT,
    GH_APP_EVENT_VOICE_START,
    GH_APP_EVENT_VOICE_LEVEL,
    GH_APP_EVENT_VOICE_TRANSCRIBING,
    GH_APP_EVENT_VOICE_COMPLETE,
    GH_APP_EVENT_VOICE_FAILED,
    GH_APP_EVENT_NAVIGATE,
} gh_app_event_type_t;

typedef struct {
    gh_app_event_type_t type;
    uint32_t timestamp_ms;
    uint16_t rms;
    uint16_t peak;
    bool speech;
    gh_screen_t screen;
    gh_backend_voice_result_t voice_result;
    char error[80];
} gh_app_event_t;

static QueueHandle_t s_app_events;
static TimerHandle_t s_explain_timer;
static TaskHandle_t s_voice_task;

#ifdef CONFIG_GH_ALLOW_INSECURE_HTTP
#define GH_INSECURE_HTTP_ENABLED true
#else
#define GH_INSECURE_HTTP_ENABLED false
#endif

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_wifi_has_credentials) esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "ESP32-S3 Wi-Fi disconnected; reconnecting");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = event_data;
        ESP_LOGI(TAG, "ESP32-S3 Wi-Fi connected; IPv4=" IPSTR,
                 IP2STR(&got_ip->ip_info.ip));
        gh_ble_provisioning_report_wifi(true);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_start(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init failed");
    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    wifi_event_handler, NULL), TAG,
                        "Wi-Fi event registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                    wifi_event_handler, NULL), TAG,
                        "IP event registration failed");
    wifi_config_t config = {0};
    char ssid[GH_WIFI_SSID_MAX] = {0};
    char password[GH_WIFI_PASSWORD_MAX] = {0};
    if (!gh_ble_provisioning_load_wifi(ssid, sizeof(ssid), password, sizeof(password)) &&
        CONFIG_GH_WIFI_SSID[0] != '\0') {
        strlcpy(ssid, CONFIG_GH_WIFI_SSID, sizeof(ssid));
        strlcpy(password, CONFIG_GH_WIFI_PASSWORD, sizeof(password));
        ESP_LOGW(TAG, "ESP32-S3 is using HIL build Wi-Fi until BLE provisioning completes");
    }
    s_wifi_has_credentials = ssid[0] != '\0';
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password, sizeof(config.sta.password));
    memset(password, 0, sizeof(password));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "Wi-Fi config failed");
    return esp_wifi_start();
}

static void apply_pending_wifi(void)
{
    char ssid[GH_WIFI_SSID_MAX] = {0};
    char password[GH_WIFI_PASSWORD_MAX] = {0};
    if (!gh_ble_provisioning_take_pending(ssid, sizeof(ssid),
                                          password, sizeof(password))) return;
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password, sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    memset(password, 0, sizeof(password));
    if (err != ESP_OK) {
        gh_ble_provisioning_report_wifi(false);
        ESP_LOGE(TAG, "ESP32-S3 could not apply BLE Wi-Fi configuration: %s",
                 esp_err_to_name(err));
        return;
    }
    s_wifi_has_credentials = true;
    s_time_ready_logged = false;
    err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_CONNECT) {
        err = esp_wifi_connect();
        if (err != ESP_OK) gh_ble_provisioning_report_wifi(false);
    } else if (err != ESP_OK) {
        gh_ble_provisioning_report_wifi(false);
        ESP_LOGE(TAG, "ESP32-S3 could not restart Wi-Fi after BLE provisioning: %s",
                 esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "ESP32-S3 applying BLE Wi-Fi configuration; secrets omitted");
}

static bool backend_uses_https(void)
{
    return strncmp(CONFIG_GH_BACKEND_BASE_URL, "https://", 8) == 0;
}

static bool system_time_is_trustworthy(void)
{
    const time_t now = time(NULL);
    return now >= 1704067200; /* 2024-01-01T00:00:00Z */
}

static void log_trustworthy_time_once(const char *state)
{
    if (s_time_ready_logged) return;
    const time_t synchronized_at = time(NULL);
    struct tm utc = {0};
    gmtime_r(&synchronized_at, &utc);
    ESP_LOGI(TAG,
             "ESP32-S3 SNTP time %s; UTC=%04d-%02d-%02dT%02d:%02d:%02dZ",
             state, utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
             utc.tm_hour, utc.tm_min, utc.tm_sec);
    s_time_ready_logged = true;
}

static esp_err_t ensure_secure_network_ready(void)
{
    /* An empty Wi-Fi configuration is a valid commissioning state.  Keep the
     * ESP32-S3 UI available instead of waiting on an event group that has not
     * been created yet. */
    if (s_wifi_events == NULL) {
        s_model.connection_state = GH_CONNECTION_OFFLINE;
        gh_model_route_runtime(&s_model);
        return ESP_ERR_INVALID_STATE;
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        s_model.connection_state = GH_CONNECTION_CONNECTING;
        gh_model_route_runtime(&s_model);
        return ESP_ERR_TIMEOUT;
    }
    if (!backend_uses_https()) {
        s_model.connection_state = GH_CONNECTION_SECURE_READY;
        return ESP_OK;
    }
    if (system_time_is_trustworthy()) {
        log_trustworthy_time_once("ready");
        s_model.connection_state = GH_CONNECTION_SECURE_READY;
        return ESP_OK;
    }

    s_model.connection_state = GH_CONNECTION_TIME_SYNCING;
    gh_model_route_runtime(&s_model);
    gh_ui_render(&s_model);
    if (!s_sntp_started) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_GH_SNTP_SERVER);
        ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&config), TAG, "SNTP init failed");
        s_sntp_started = true;
    }
    ESP_RETURN_ON_ERROR(esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)), TAG,
                        "SNTP sync failed");
    if (!system_time_is_trustworthy()) {
        return ESP_ERR_INVALID_STATE;
    }
    log_trustworthy_time_once("synchronized");
    s_model.connection_state = GH_CONNECTION_SECURE_READY;
    return ESP_OK;
}

static void approval_requested(void *context)
{
    gh_app_model_t *model = context;
    if (!gh_model_can_approve(model)) {
        ESP_LOGW(TAG, "Approval rejected by local precondition check");
        return;
    }
    esp_err_t err = gh_backend_approve(model);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ESP32-S3 Action Set approval rejected; action_set_id=%d error=%s",
                 model->action_set_id, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 Long-press Approval | "
             "PASS | action_set_id=%d proposal_id=%s",
             model->action_set_id, model->proposal.proposal_id);
    if (gh_model_begin_approval(model)) {
        gh_ui_render(model);
    }
}

static void refresh_companion_power(void)
{
    gh_companion_power_t power = {0};
    esp_err_t err = gh_peripherals_read_power(&power);
    s_model.energy.companion_battery_present = err == ESP_OK && power.battery_present;
    s_model.energy.companion_battery_pct_available =
        err == ESP_OK && power.percentage_available;
    s_model.energy.companion_battery_voltage_available =
        err == ESP_OK && power.voltage_available;
    s_model.energy.companion_battery_charging = err == ESP_OK && power.charging;
    if (power.percentage_available) {
        s_model.energy.companion_battery_pct = (float)power.percentage;
    }
    if (power.voltage_available) {
        s_model.energy.companion_battery_voltage_v = power.voltage_mv / 1000.0f;
    }
    if (!s_power_hil_logged) {
        if (err == ESP_OK) {
            ESP_LOGI(TAG,
                     "ESP32-S3 Hardware-in-the-loop | ESP32-S3 AXP2101 Runtime "
                     "Power | PASS | present=%s percentage=%s voltage=%s charging=%s",
                     power.battery_present ? "true" : "false",
                     power.percentage_available ? "available" : "unavailable",
                     power.voltage_available ? "available" : "unavailable",
                     power.charging ? "true" : "false");
        } else {
            ESP_LOGW(TAG, "ESP32-S3 AXP2101 runtime power unavailable: %s",
                     esp_err_to_name(err));
        }
        s_power_hil_logged = true;
    }
}

static void imu_lift_event(uint32_t timestamp_ms, void *context)
{
    (void)context;
    if (s_app_events == NULL) return;
    const gh_app_event_t event = {
        .type = GH_APP_EVENT_LIFT,
        .timestamp_ms = timestamp_ms,
    };
    /* The IMU task publishes an event only. It never touches Model or LVGL. */
    xQueueSend(s_app_events, &event, 0);
}

static void explain_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    if (s_app_events == NULL) return;
    const gh_app_event_t event = {.type = GH_APP_EVENT_EXPLAIN_TIMEOUT};
    xQueueSend(s_app_events, &event, 0);
}

static void voice_level_event(uint16_t rms, uint16_t peak, bool speech,
                              void *context)
{
    (void)context;
    if (s_app_events == NULL) return;
    const gh_app_event_t event = {
        .type = GH_APP_EVENT_VOICE_LEVEL,
        .rms = rms,
        .peak = peak,
        .speech = speech,
    };
    xQueueSend(s_app_events, &event, 0);
}

static void voice_session_task(void *argument)
{
    (void)argument;
    gh_voice_capture_t capture = {0};
    gh_app_event_t event = {0};
    esp_err_t err = gh_audio_capture_voice(&capture, voice_level_event, NULL);
    if (err != ESP_OK || !capture.speech_detected) {
        event.type = GH_APP_EVENT_VOICE_FAILED;
        snprintf(event.error, sizeof(event.error), "%s",
                 err == ESP_OK ? "没听清，请再说一次。" : "麦克风暂时不可用。");
        xQueueSend(s_app_events, &event, portMAX_DELAY);
        gh_audio_capture_free(&capture);
        s_voice_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    event.type = GH_APP_EVENT_VOICE_TRANSCRIBING;
    xQueueSend(s_app_events, &event, portMAX_DELAY);
    memset(&event, 0, sizeof(event));
    err = gh_backend_voice(capture.wav_data, capture.wav_size,
                           &event.voice_result);
    gh_audio_capture_free(&capture);
    if (err == ESP_OK) {
        event.type = GH_APP_EVENT_VOICE_COMPLETE;
    } else {
        event.type = GH_APP_EVENT_VOICE_FAILED;
        snprintf(event.error, sizeof(event.error), "%s",
                 "语音服务暂时不可用，请稍后重试。");
    }
    xQueueSend(s_app_events, &event, portMAX_DELAY);
    s_voice_task = NULL;
    vTaskDelete(NULL);
}

static void voice_requested(void *context)
{
    (void)context;
    if (s_model.voice_state == GH_VOICE_LISTENING) {
        gh_audio_request_stop();
        return;
    }
    if (s_voice_task != NULL || s_app_events == NULL) return;
    const gh_app_event_t event = {.type = GH_APP_EVENT_VOICE_START};
    xQueueSend(s_app_events, &event, 0);
}

static void navigation_requested(gh_screen_t screen, void *context)
{
    (void)context;
    if (s_app_events == NULL || screen >= GH_SCREEN_COUNT) return;
    const gh_app_event_t event = {
        .type = GH_APP_EVENT_NAVIGATE,
        .screen = screen,
    };
    xQueueSend(s_app_events, &event, 0);
}

static void app_event_task(void *argument)
{
    (void)argument;
    gh_app_event_t event = {0};
    while (true) {
        if (xQueueReceive(s_app_events, &event, portMAX_DELAY) != pdTRUE) continue;
        if (event.type == GH_APP_EVENT_LIFT) {
            const gh_screen_t before = s_model.screen;
            gh_model_set_lifted(&s_model, true);
            gh_ui_render(&s_model);
            if (s_model.screen == GH_SCREEN_EXPLAIN && s_explain_timer != NULL) {
                xTimerReset(s_explain_timer, 0);
            }
            ESP_LOGI(TAG,
                     "ESP32-S3 Lift Event -> Model -> UI before=%d after=%d "
                     "timestamp_ms=%lu approval=false",
                     before, s_model.screen, (unsigned long)event.timestamp_ms);
        } else if (event.type == GH_APP_EVENT_EXPLAIN_TIMEOUT) {
            gh_model_set_lifted(&s_model, false);
            gh_ui_render(&s_model);
            ESP_LOGI(TAG, "ESP32-S3 Explain timeout -> Ambient");
        } else if (event.type == GH_APP_EVENT_VOICE_START) {
            if (s_model.backend_online && !s_model.energy.stale &&
                s_voice_task == NULL) {
                gh_model_voice_begin(&s_model);
                gh_ui_render(&s_model);
                if (xTaskCreate(voice_session_task, "gh_voice", 7168, NULL, 6,
                                &s_voice_task) != pdPASS) {
                    s_voice_task = NULL;
                    gh_model_voice_fail(&s_model, "语音任务无法启动。");
                    gh_ui_render(&s_model);
                }
            }
        } else if (event.type == GH_APP_EVENT_VOICE_LEVEL) {
            gh_model_voice_level(&s_model, event.rms, event.peak, event.speech);
            gh_ui_render(&s_model);
        } else if (event.type == GH_APP_EVENT_VOICE_TRANSCRIBING) {
            gh_model_voice_transcribing(&s_model);
            gh_ui_render(&s_model);
        } else if (event.type == GH_APP_EVENT_VOICE_COMPLETE) {
            /* Never perform the HTTPS/JSON refresh on the UI event task: that
             * path needs substantially more stack and previously overflowed
             * immediately after a successful voice upload.  The dedicated
             * refresh task remains the single owner of snapshot refreshes. */
            gh_model_voice_complete(&s_model,
                                    event.voice_result.transcript,
                                    event.voice_result.answer,
                                    event.voice_result.intent,
                                    event.voice_result.requires_touch_approval);
            gh_ui_render(&s_model);
        } else if (event.type == GH_APP_EVENT_VOICE_FAILED) {
            gh_model_voice_fail(&s_model, event.error);
            gh_ui_render(&s_model);
        } else if (event.type == GH_APP_EVENT_NAVIGATE) {
            const gh_screen_t before = s_model.screen;
            gh_model_open_screen(&s_model, event.screen);
            gh_ui_render(&s_model);
            ESP_LOGI(TAG,
                     "ESP32-S3 UI navigation queued before=%d after=%d "
                     "action_set_id=%d acknowledged_action_set_id=%d",
                     before, s_model.screen, s_model.action_set_id,
                     s_model.acknowledged_action_set_id);
        }
    }
}

static void app_refresh_task(void *argument)
{
    (void)argument;
    while (true) {
        apply_pending_wifi();
        refresh_companion_power();
        esp_err_t network_result = ensure_secure_network_ready();
        if (network_result != ESP_OK) {
            ESP_LOGW(TAG, "Secure network not ready: %s", esp_err_to_name(network_result));
            s_model.backend_online = false;
            s_model.websocket_online = false;
            s_model.agent_state = GH_AGENT_OFFLINE;
            s_model.energy.stale = true;
            gh_model_route_runtime(&s_model);
            ESP_LOGW(TAG,
                     "ESP32-S3 snapshot stale=true last_successful_sync_at=%" PRId64,
                     s_model.energy.last_successful_sync_at);
            gh_ui_render(&s_model);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (!s_model.backend_online) {
            s_model.connection_state = GH_CONNECTION_BACKEND_CONNECTING;
            gh_model_route_runtime(&s_model);
            gh_ui_render(&s_model);
            gh_backend_pair_if_needed(&s_model);
        }
        esp_err_t err = gh_backend_refresh(&s_model);
        if (err == ESP_OK) {
            s_model.connection_state = GH_CONNECTION_SECURE_READY;
        }
        if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Refresh failed: %s", esp_err_to_name(err));
        }
        gh_model_route_runtime(&s_model);
        gh_ui_render(&s_model);
        vTaskDelay(pdMS_TO_TICKS(BACKEND_REFRESH_INTERVAL_MS));
    }
}

esp_err_t gh_app_start(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    gh_model_init(&s_model);
    s_model.connection_state = GH_CONNECTION_OFFLINE;
    ESP_LOGI(TAG, "ESP32-S3 reset reason=%d", (int)esp_reset_reason());

    gh_backend_config_t backend_config = {
        .base_url = CONFIG_GH_BACKEND_BASE_URL,
        .device_id = CONFIG_GH_DEVICE_ID,
        .allow_insecure_http = GH_INSECURE_HTTP_ENABLED,
    };
    ESP_RETURN_ON_ERROR(gh_backend_init(&backend_config), TAG, "backend init failed");

    esp_err_t hall_result = gh_hall_init();
    if (hall_result != ESP_OK && hall_result != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Hall sensor unavailable: %s", esp_err_to_name(hall_result));
    }

    ESP_RETURN_ON_ERROR(
        gh_ui_start(&s_model, approval_requested, voice_requested,
                    navigation_requested, &s_model),
        TAG, "UI init failed");
    esp_err_t peripheral_result = gh_peripherals_run_hil();
    if (peripheral_result != ESP_OK) {
        /* Keep the AMOLED diagnostic page alive, but do not continue into the
         * network/backend stages when a frozen-order hardware gate fails. */
        ESP_LOGE(TAG, "ESP32-S3 board peripheral HIL stopped: %s",
                 esp_err_to_name(peripheral_result));
        return ESP_OK;
    }
    esp_err_t audio_result = gh_audio_init();
    if (audio_result != ESP_OK) {
        /* Audio is a frozen-order ESP32-S3 hardware gate. Keep the diagnostic
         * UI alive and do not advance into Wi-Fi/backend linkage on failure. */
        ESP_LOGE(TAG, "ESP32-S3 audio HIL stopped: %s",
                 esp_err_to_name(audio_result));
        return ESP_OK;
    }
#ifdef CONFIG_GH_BOOT_AUDIO_HIL
    audio_result = gh_audio_run_hil();
    if (audio_result != ESP_OK) {
        ESP_LOGE(TAG, "ESP32-S3 audio manufacturing HIL stopped: %s",
                 esp_err_to_name(audio_result));
        return ESP_OK;
    }
#else
    ESP_LOGI(TAG,
             "ESP32-S3 ES8311/I2S runtime initialized; boot speaker tone disabled");
#endif
    s_app_events = xQueueCreate(12, sizeof(gh_app_event_t));
    if (s_app_events == NULL) return ESP_ERR_NO_MEM;
    s_explain_timer = xTimerCreate("gh_explain", pdMS_TO_TICKS(EXPLAIN_RETURN_MS),
                                   pdFALSE, NULL, explain_timer_callback);
    if (s_explain_timer == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreate(app_event_task, "gh_events", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t imu_result = gh_imu_start(imu_lift_event, NULL);
    if (imu_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "ESP32-S3 QMI8658 runtime unavailable; Lift-to-Explain disabled: %s",
                 esp_err_to_name(imu_result));
    }
    esp_err_t wifi_result = wifi_start();
    if (wifi_result != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi linkage not started: %s", esp_err_to_name(wifi_result));
    } else {
        s_model.connection_state = GH_CONNECTION_CONNECTING;
    }
    esp_err_t ble_result = gh_ble_provisioning_start();
    if (ble_result != ESP_OK) {
        ESP_LOGW(TAG, "ESP32-S3 BLE provisioning unavailable: %s",
                 esp_err_to_name(ble_result));
    }
    if (xTaskCreate(app_refresh_task, "gh_refresh", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
