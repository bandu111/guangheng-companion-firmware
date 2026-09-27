#include "gh_backend.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "gh_snapshot_parser.h"

static const char *TAG = "gh_backend";
static gh_backend_config_t s_config;
static char s_device_id[80];
static char s_credential[128];
static char s_pairing_code[8];
static char s_poll_token[96];
static int s_last_http_status;
static esp_http_client_handle_t s_http_client;
static SemaphoreHandle_t s_http_mutex;

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} response_buffer_t;

static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_buffer_t *buffer = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0 || buffer == NULL) {
        return ESP_OK;
    }
    size_t needed = buffer->size + (size_t)event->data_len + 1;
    if (needed > buffer->capacity) {
        size_t capacity = needed * 2;
        char *next = realloc(buffer->data, capacity);
        if (next == NULL) return ESP_ERR_NO_MEM;
        buffer->data = next;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->size, event->data, event->data_len);
    buffer->size += event->data_len;
    buffer->data[buffer->size] = '\0';
    return ESP_OK;
}

static bool transport_allowed(void)
{
    if (s_config.base_url == NULL || s_config.base_url[0] == '\0') return false;
    return strncmp(s_config.base_url, "https://", 8) == 0 || s_config.allow_insecure_http;
}

static esp_err_t request_json(const char *method, const char *path, const char *body,
                              bool authenticated, cJSON **result)
{
    if (!transport_allowed()) {
        ESP_LOGE(TAG, "Backend transport blocked: HTTPS required (or explicit demo HTTP opt-in)");
        return ESP_ERR_INVALID_STATE;
    }
    char url[256];
    snprintf(url, sizeof(url), "%s%s", s_config.base_url, path);
    response_buffer_t response = {0};
    if (s_http_client == NULL || s_http_mutex == NULL) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_http_mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_http_client_set_user_data(s_http_client, &response);
    esp_http_client_set_url(s_http_client, url);
    esp_http_client_set_method(s_http_client,
        strcmp(method, "POST") == 0 ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    esp_http_client_set_header(s_http_client, "Content-Type", "application/json");
    esp_http_client_delete_header(s_http_client, "X-Companion-Device-Id");
    esp_http_client_delete_header(s_http_client, "X-Companion-Credential");
    if (authenticated) {
        esp_http_client_set_header(s_http_client, "X-Companion-Device-Id", s_device_id);
        esp_http_client_set_header(s_http_client, "X-Companion-Credential", s_credential);
    }
    esp_http_client_set_post_field(s_http_client, body, body == NULL ? 0 : strlen(body));
    esp_err_t err = esp_http_client_perform(s_http_client);
    int status = esp_http_client_get_status_code(s_http_client);
    s_last_http_status = status;
    /*
     * Keep the configured client object, but release the TLS transport after
     * every completed request.  The CO5300 SPI driver needs a contiguous
     * internal-DMA bounce buffer while LVGL flushes; retaining the live TLS
     * session starved that allocation on ESP32-S3.  Reusing the client object
     * avoids the client-allocation churn that previously fragmented the heap,
     * while close() returns the large TLS transport buffers before UI redraw.
     */
    esp_http_client_close(s_http_client);
    esp_http_client_set_user_data(s_http_client, NULL);
    xSemaphoreGive(s_http_mutex);
    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "Backend request failed: status=%d error=%s", status, esp_err_to_name(err));
        free(response.data);
        return err == ESP_OK ? ESP_FAIL : err;
    }
    ESP_LOGI(TAG, "ESP32-S3 HTTPS request accepted: %s %s status=%d",
             method, path, status);
    *result = cJSON_Parse(response.data ? response.data : "{}");
    free(response.data);
    return *result == NULL ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
}

static esp_err_t request_audio(const uint8_t *audio, size_t audio_size,
                               cJSON **result)
{
    if (!transport_allowed() || audio == NULL || audio_size == 0 ||
        s_credential[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    char url[256];
    snprintf(url, sizeof(url), "%s/api/v1/companion/voice", s_config.base_url);
    response_buffer_t response = {0};
    if (xSemaphoreTake(s_http_mutex, pdMS_TO_TICKS(60000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_http_client_set_user_data(s_http_client, &response);
    esp_http_client_set_url(s_http_client, url);
    esp_http_client_set_method(s_http_client, HTTP_METHOD_POST);
    esp_http_client_set_header(s_http_client, "Content-Type", "audio/wav");
    esp_http_client_set_header(s_http_client, "X-Companion-Device-Id", s_device_id);
    esp_http_client_set_header(s_http_client, "X-Companion-Credential", s_credential);
    esp_http_client_set_post_field(s_http_client, (const char *)audio, audio_size);
    esp_http_client_set_timeout_ms(s_http_client, 60000);
    esp_err_t err = esp_http_client_perform(s_http_client);
    const int status = esp_http_client_get_status_code(s_http_client);
    s_last_http_status = status;
    esp_http_client_close(s_http_client);
    esp_http_client_set_timeout_ms(s_http_client, 12000);
    esp_http_client_set_user_data(s_http_client, NULL);
    xSemaphoreGive(s_http_mutex);
    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "ESP32-S3 voice upload failed status=%d error=%s",
                 status, esp_err_to_name(err));
        free(response.data);
        return err == ESP_OK ? ESP_FAIL : err;
    }
    *result = cJSON_Parse(response.data ? response.data : "{}");
    free(response.data);
    ESP_LOGI(TAG,
             "ESP32-S3 HTTPS voice upload accepted status=%d bytes=%u",
             status, (unsigned)audio_size);
    return *result == NULL ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
}

static void load_credential(void)
{
    nvs_handle_t nvs;
    if (nvs_open("gh_companion", NVS_READONLY, &nvs) != ESP_OK) return;
    size_t length = sizeof(s_credential);
    nvs_get_str(nvs, "credential", s_credential, &length);
    length = sizeof(s_device_id);
    nvs_get_str(nvs, "device_id", s_device_id, &length);
    nvs_close(nvs);
}

static esp_err_t save_credential(const char *device_id, const char *credential)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open("gh_companion", NVS_READWRITE, &nvs), TAG, "NVS open");
    esp_err_t err = nvs_set_str(nvs, "device_id", device_id);
    if (err == ESP_OK) err = nvs_set_str(nvs, "credential", credential);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) {
        strlcpy(s_device_id, device_id, sizeof(s_device_id));
        strlcpy(s_credential, credential, sizeof(s_credential));
    }
    return err;
}

static esp_err_t clear_revoked_credential(void)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open("gh_companion", NVS_READWRITE, &nvs), TAG, "NVS open");
    esp_err_t err = nvs_erase_key(nvs, "credential");
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) {
        s_credential[0] = '\0';
        s_pairing_code[0] = '\0';
        s_poll_token[0] = '\0';
        ESP_LOGW(TAG,
                 "ESP32-S3 authorization revoked; local credential cleared and re-pairing required");
    }
    return err;
}

esp_err_t gh_backend_init(const gh_backend_config_t *config)
{
    if (config == NULL || config->device_id == NULL) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    strlcpy(s_device_id, config->device_id, sizeof(s_device_id));
    load_credential();
    s_http_mutex = xSemaphoreCreateMutex();
    if (s_http_mutex == NULL) return ESP_ERR_NO_MEM;
    esp_http_client_config_t http_config = {
        .url = s_config.base_url,
        .event_handler = http_event,
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = false,
        .keep_alive_enable = true,
        .keep_alive_idle = 10,
        .keep_alive_interval = 5,
        .keep_alive_count = 3,
    };
    s_http_client = esp_http_client_init(&http_config);
    if (s_http_client == NULL) {
        vSemaphoreDelete(s_http_mutex);
        s_http_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool gh_backend_is_configured(void)
{
    return transport_allowed();
}

esp_err_t gh_backend_pair_if_needed(gh_app_model_t *model)
{
    if (s_credential[0] != '\0') return ESP_OK;
    cJSON *json = NULL;
    if (s_pairing_code[0] == '\0') {
        cJSON *request = cJSON_CreateObject();
        cJSON_AddStringToObject(request, "device_uid", s_device_id);
        cJSON_AddStringToObject(request, "display_name", "GuangHeng Energy Companion");
        char *body = cJSON_PrintUnformatted(request);
        esp_err_t err = request_json("POST", "/api/v1/companion/pairing/device-code", body,
                                     false, &json);
        free(body);
        cJSON_Delete(request);
        if (err != ESP_OK) return err;
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(json, "code");
        const cJSON *poll = cJSON_GetObjectItemCaseSensitive(json, "poll_token");
        if (!cJSON_IsString(code) || !cJSON_IsString(poll)) {
            cJSON_Delete(json);
            return ESP_ERR_INVALID_RESPONSE;
        }
        strlcpy(s_pairing_code, code->valuestring, sizeof(s_pairing_code));
        strlcpy(s_poll_token, poll->valuestring, sizeof(s_poll_token));
        strlcpy(model->pairing_code, s_pairing_code, sizeof(model->pairing_code));
        model->generation++;
        ESP_LOGI(TAG, "ESP32-S3 pairing code created and displayed on AMOLED");
        cJSON_Delete(json);
        return ESP_OK;
    }
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "code", s_pairing_code);
    cJSON_AddStringToObject(request, "poll_token", s_poll_token);
    char *body = cJSON_PrintUnformatted(request);
    esp_err_t err = request_json("POST", "/api/v1/companion/pairing/poll", body, false, &json);
    free(body);
    cJSON_Delete(request);
    if (err != ESP_OK) return err;
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(json, "status");
    if (cJSON_IsString(status) && strcmp(status->valuestring, "PAIRED") == 0) {
        const cJSON *device_id = cJSON_GetObjectItemCaseSensitive(json, "device_id");
        const cJSON *credential = cJSON_GetObjectItemCaseSensitive(json, "credential");
        if (cJSON_IsString(device_id) && cJSON_IsString(credential)) {
            err = save_credential(device_id->valuestring, credential->valuestring);
            model->pairing_code[0] = '\0';
            s_pairing_code[0] = '\0';
            s_poll_token[0] = '\0';
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "ESP32-S3 pairing completed; credential stored in NVS");
            }
        }
    }
    cJSON_Delete(json);
    return err;
}

esp_err_t gh_backend_refresh(gh_app_model_t *model)
{
    if (model == NULL) return ESP_ERR_INVALID_ARG;
    if (s_credential[0] == '\0') return ESP_ERR_INVALID_STATE;
    cJSON *root = NULL;
    esp_err_t err = request_json("GET", "/api/v1/companion/snapshot", NULL, true, &root);
    if (err != ESP_OK) {
        if (s_last_http_status == 401 || s_last_http_status == 403) {
            esp_err_t clear_err = clear_revoked_credential();
            if (clear_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to clear revoked credential: %s",
                         esp_err_to_name(clear_err));
            }
        }
        gh_model_set_offline(model);
        ESP_LOGW(TAG,
                 "ESP32-S3 snapshot stale=true last_successful_sync_at=%" PRId64,
                 model->energy.last_successful_sync_at);
        return err;
    }
    if (!gh_snapshot_apply_energy(root, model)) {
        cJSON_Delete(root);
        gh_model_set_offline(model);
        ESP_LOGW(TAG,
                 "ESP32-S3 snapshot stale=true last_successful_sync_at=%" PRId64,
                 model->energy.last_successful_sync_at);
        return ESP_ERR_INVALID_RESPONSE;
    }
    const int64_t observed_at = (int64_t)time(NULL);
    gh_model_mark_snapshot_fresh(model, observed_at);
    model->websocket_online = false;

    const bool action_set_valid =
        gh_snapshot_apply_pending_action_set(root, model, observed_at);
    const bool runtime_action_set_valid =
        gh_snapshot_apply_current_action_set(root, model);
    if (!action_set_valid) {
        ESP_LOGW(TAG,
                 "ESP32-S3 pending Action Set absent or invalid; local approval disabled");
    } else if (model->proposal.pending) {
        ESP_LOGI(TAG,
                 "ESP32-S3 pending Action Set parsed; action_set_id=%d "
                 "proposal_id=%s actions=%u status=%s expired=%s",
                 model->action_set_id, model->proposal.proposal_id,
                 (unsigned)model->proposal.action_count, model->proposal.status,
                 model->proposal.expired ? "true" : "false");
    }
    if (!runtime_action_set_valid) {
        ESP_LOGW(TAG, "ESP32-S3 current Action Set runtime payload invalid");
    }
    model->generation++;
    ESP_LOGI(TAG,
             "ESP32-S3 backend snapshot received; source_mode=%s stale=false",
             gh_source_mode_label(model->energy.source_mode));
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t gh_backend_approve(const gh_app_model_t *model)
{
    if (!gh_model_can_approve(model) || model->approval_nonce[0] == '\0' ||
        model->action_set_id <= 0) return ESP_ERR_INVALID_STATE;
    char path[128];
    snprintf(path, sizeof(path), "/api/v1/companion/action-sets/%d/approve",
             model->action_set_id);
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "nonce", model->approval_nonce);
    cJSON_AddStringToObject(request, "action_set_version", model->action_set_version);
    char *body = cJSON_PrintUnformatted(request);
    cJSON *response = NULL;
    esp_err_t err = request_json("POST", path, body, true, &response);
    free(body);
    cJSON_Delete(request);
    cJSON_Delete(response);
    return err;
}

esp_err_t gh_backend_voice(const uint8_t *wav_data, size_t wav_size,
                           gh_backend_voice_result_t *voice_result)
{
    if (voice_result == NULL) return ESP_ERR_INVALID_ARG;
    memset(voice_result, 0, sizeof(*voice_result));
    cJSON *root = NULL;
    esp_err_t err = request_audio(wav_data, wav_size, &root);
    if (err != ESP_OK) return err;
    const cJSON *transcript = cJSON_GetObjectItemCaseSensitive(root, "transcript");
    const cJSON *answer = cJSON_GetObjectItemCaseSensitive(root, "answer");
    const cJSON *intent = cJSON_GetObjectItemCaseSensitive(root, "intent");
    const cJSON *requires_touch =
        cJSON_GetObjectItemCaseSensitive(root, "requires_touch_approval");
    if (!cJSON_IsString(transcript) || !cJSON_IsString(intent)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    strlcpy(voice_result->transcript, transcript->valuestring,
            sizeof(voice_result->transcript));
    strlcpy(voice_result->answer,
            cJSON_IsString(answer) ? answer->valuestring : "",
            sizeof(voice_result->answer));
    strlcpy(voice_result->intent, intent->valuestring,
            sizeof(voice_result->intent));
    voice_result->requires_touch_approval = cJSON_IsTrue(requires_touch);
    ESP_LOGI(TAG,
             "ESP32-S3 voice result intent=%s requires_touch=%s "
             "approval_performed=false",
             voice_result->intent,
             voice_result->requires_touch_approval ? "true" : "false");
    cJSON_Delete(root);
    return ESP_OK;
}
