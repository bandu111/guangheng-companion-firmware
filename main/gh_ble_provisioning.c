#include "gh_ble_provisioning.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "gh_ui.h"

static const char *TAG = "gh_ble";
static const char *NVS_NAMESPACE = "gh_wifi";

#define GH_SVC_BYTES 0x10,0x2a,0x47,0x5f,0x0d,0x8c,0xce,0xb9,0x92,0x4a,0x7c,0x8f,0x01,0x00,0x51,0x7f
#define GH_RX_BYTES  0x10,0x2a,0x47,0x5f,0x0d,0x8c,0xce,0xb9,0x92,0x4a,0x7c,0x8f,0x02,0x00,0x51,0x7f
#define GH_ST_BYTES  0x10,0x2a,0x47,0x5f,0x0d,0x8c,0xce,0xb9,0x92,0x4a,0x7c,0x8f,0x03,0x00,0x51,0x7f

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(GH_SVC_BYTES);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(GH_RX_BYTES);
static const ble_uuid128_t s_status_uuid = BLE_UUID128_INIT(GH_ST_BYTES);
static uint8_t s_own_addr_type;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_pending_ssid[GH_WIFI_SSID_MAX];
static char s_pending_password[GH_WIFI_PASSWORD_MAX];
static bool s_pending;
static char s_status[24] = "ready";

extern void ble_store_config_init(void);

static void set_status(const char *value)
{
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_status, value, sizeof(s_status));
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t save_wifi(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_str(nvs, "ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(nvs, "password", password);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

bool gh_ble_provisioning_load_wifi(char *ssid, size_t ssid_size,
                                   char *password, size_t password_size)
{
    nvs_handle_t nvs;
    if (!ssid || !password || ssid_size == 0 || password_size == 0 ||
        nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;
    size_t ssid_len = ssid_size;
    size_t password_len = password_size;
    esp_err_t a = nvs_get_str(nvs, "ssid", ssid, &ssid_len);
    esp_err_t b = nvs_get_str(nvs, "password", password, &password_len);
    nvs_close(nvs);
    return a == ESP_OK && b == ESP_OK && ssid[0] != '\0';
}

bool gh_ble_provisioning_take_pending(char *ssid, size_t ssid_size,
                                      char *password, size_t password_size)
{
    bool available;
    portENTER_CRITICAL(&s_lock);
    available = s_pending;
    if (available) {
        strlcpy(ssid, s_pending_ssid, ssid_size);
        strlcpy(password, s_pending_password, password_size);
        memset(s_pending_password, 0, sizeof(s_pending_password));
        s_pending = false;
    }
    portEXIT_CRITICAL(&s_lock);
    return available;
}

void gh_ble_provisioning_report_wifi(bool connected)
{
    set_status(connected ? "connected" : "failed");
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char value[sizeof(s_status)];
        portENTER_CRITICAL(&s_lock);
        strlcpy(value, s_status, sizeof(value));
        portEXIT_CRITICAL(&s_lock);
        return os_mbuf_append(ctxt->om, value, strlen(value)) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    const uint16_t length = OS_MBUF_PKTLEN(ctxt->om);
    if (length == 0 || length > 180) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    char payload[181];
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, payload, length, &copied) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    payload[copied] = '\0';
    cJSON *json = cJSON_Parse(payload);
    const cJSON *ssid = json ? cJSON_GetObjectItemCaseSensitive(json, "ssid") : NULL;
    const cJSON *password = json ? cJSON_GetObjectItemCaseSensitive(json, "password") : NULL;
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password) ||
        ssid->valuestring[0] == '\0' || strlen(ssid->valuestring) >= GH_WIFI_SSID_MAX ||
        strlen(password->valuestring) >= GH_WIFI_PASSWORD_MAX) {
        cJSON_Delete(json);
        set_status("invalid");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    esp_err_t err = save_wifi(ssid->valuestring, password->valuestring);
    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        strlcpy(s_pending_ssid, ssid->valuestring, sizeof(s_pending_ssid));
        strlcpy(s_pending_password, password->valuestring, sizeof(s_pending_password));
        s_pending = true;
        portEXIT_CRITICAL(&s_lock);
        set_status("received");
        ESP_LOGI(TAG, "ESP32-S3 BLE Wi-Fi credentials stored in NVS; secrets omitted");
    } else {
        set_status("storage_error");
    }
    cJSON_Delete(json);
    return err == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_chr_def s_characteristics[] = {
    {
        .uuid = &s_rx_uuid.u,
        .access_cb = gatt_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC |
                 BLE_GATT_CHR_F_WRITE_AUTHEN,
    },
    {
        .uuid = &s_status_uuid.u,
        .access_cb = gatt_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                 BLE_GATT_CHR_F_READ_AUTHEN,
    },
    {0},
};

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = s_characteristics,
    },
    {0},
};

static int gap_event(struct ble_gap_event *event, void *arg);

static int advertise(void)
{
    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids128 = (ble_uuid128_t *)&s_service_uuid,
        .num_uuids128 = 1,
        .uuids128_is_complete = 1,
    };
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;
    const char *name = "GuangHeng Companion";
    struct ble_hs_adv_fields response = {
        .name = (uint8_t *)name,
        .name_len = strlen(name),
        .name_is_complete = 1,
    };
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) return rc;
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    return ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                             &params, gap_event, NULL);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ble_gap_security_initiate(event->connect.conn_handle);
        } else {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
    case BLE_GAP_EVENT_ADV_COMPLETE:
        gh_ui_hide_ble_passkey();
        advertise();
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            uint32_t passkey = esp_random() % 1000000U;
            struct ble_sm_io value = {
                .action = BLE_SM_IOACT_DISP,
                .passkey = passkey,
            };
            gh_ui_show_ble_passkey(passkey);
            return ble_sm_inject_io(event->passkey.conn_handle, &value);
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) gh_ui_hide_ble_passkey();
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
        return 0;
    }
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 ||
        ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        ESP_LOGE(TAG, "ESP32-S3 BLE identity unavailable");
        return;
    }
    int rc = advertise();
    if (rc == 0) ESP_LOGI(TAG, "ESP32-S3 encrypted BLE provisioning available");
    else ESP_LOGE(TAG, "ESP32-S3 BLE advertising failed: %d", rc);
}

static void host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t gh_ble_provisioning_start(void)
{
    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "NimBLE init failed");
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("GuangHeng Companion");
    int rc = ble_gatts_count_cfg(s_services);
    if (rc == 0) rc = ble_gatts_add_svcs(s_services);
    if (rc != 0) return ESP_FAIL;
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
