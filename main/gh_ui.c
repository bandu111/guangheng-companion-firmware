#include "gh_ui.h"
#include "gh_hardware.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_log.h"
#include "lvgl.h"
#include "sdkconfig.h"

static const char *TAG = "gh_ui";
LV_FONT_DECLARE(gh_font_chinese_14);
LV_FONT_DECLARE(gh_font_chinese_16);
static gh_app_model_t *s_model;
static gh_ui_approval_cb_t s_approval_cb;
static gh_ui_voice_cb_t s_voice_cb;
static gh_ui_navigation_cb_t s_navigation_cb;
static void *s_approval_context;
static lv_obj_t *s_pages[GH_SCREEN_COUNT];
static gh_screen_t s_visible_screen = GH_SCREEN_COUNT;
static lv_obj_t *s_source;
static lv_obj_t *s_primary_label;
static lv_obj_t *s_primary_value;
static lv_obj_t *s_secondary[3];
static lv_obj_t *s_stale;
static lv_obj_t *s_explain_body;
static lv_obj_t *s_proposal_title;
static lv_obj_t *s_proposal_reason;
static lv_obj_t *s_proposal_impact;
static lv_obj_t *s_proposal_count;
static lv_obj_t *s_approval_impact;
static lv_obj_t *s_device_rows[5];
static lv_obj_t *s_offline_sync;
static lv_obj_t *s_voice_bars[5];
static lv_obj_t *s_voice_status;
static lv_obj_t *s_voice_transcript;
static lv_obj_t *s_voice_answer;
static lv_obj_t *s_voice_intent;
static lv_obj_t *s_execution_rows[GH_MAX_ACTIONS];
static lv_obj_t *s_verification_cards[3];
static lv_obj_t *s_verification_levels[3];
static lv_obj_t *s_verification_status;
static lv_obj_t *s_verification_power;
static lv_obj_t *s_verification_summary;
static lv_obj_t *s_approve_button;
static lv_obj_t *s_pairing_page;
static lv_obj_t *s_pairing_digits[6];
static lv_obj_t *s_ble_security_page;
static lv_obj_t *s_ble_security_digits[6];
static lv_timer_t *s_hold_timer;
static uint32_t s_hold_elapsed_ms;
static lv_point_t s_swipe_start;
static bool s_swipe_tracking;
static gh_screen_t s_swipe_origin = GH_SCREEN_COUNT;

static const char *screen_name(gh_screen_t route);
static void show_screen(gh_screen_t route);

static void screen_touch_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        ESP_LOGI(TAG,
                 "ESP32-S3 Hardware-in-the-loop | ESP32-S3 Touch Input | PASS | "
                 "controller=CST816S event=pressed");
    }
}

static const char *connection_state_label(gh_connection_state_t state)
{
    switch (state) {
    case GH_CONNECTION_CONNECTING: return "正在连接";
    case GH_CONNECTION_TIME_SYNCING: return "同步时间";
    case GH_CONNECTION_SECURE_READY: return "安全连接";
    default: return "离线";
    }
}

static const char *source_mode_label(gh_source_mode_t mode)
{
    switch (mode) {
    case GH_SOURCE_SIMULATOR: return "模拟环境";
    case GH_SOURCE_REAL: return "实时数据";
    case GH_SOURCE_REPLAY: return "回放数据";
    default: return "数据来源未知";
    }
}

static const char *agent_state_label(gh_agent_state_t state)
{
    switch (state) {
    case GH_AGENT_MONITORING: return "持续观察";
    case GH_AGENT_OPPORTUNITY: return "发现机会";
    case GH_AGENT_PENDING_APPROVAL: return "等待确认";
    case GH_AGENT_EXECUTING: return "正在执行";
    case GH_AGENT_VERIFIED: return "验证成功";
    case GH_AGENT_FAILED: return "执行失败";
    default: return "暂未连接";
    }
}

static lv_obj_t *active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}

static void style_text(lv_obj_t *object, uint32_t color, const lv_font_t *font)
{
    lv_obj_set_style_text_color(object, lv_color_hex(color), LV_PART_MAIN);
    if (font != NULL) {
        lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
    }
}

static lv_obj_t *make_card(lv_obj_t *parent, int y, int height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 336, height);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x101c2c), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x152d43), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x294966), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *make_text(lv_obj_t *parent, const char *text, uint32_t color,
                           const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    style_text(label, color, font);
    return label;
}

static void create_pairing_page(lv_obj_t *screen)
{
    s_pairing_page = lv_obj_create(screen);
    lv_obj_set_size(s_pairing_page, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_pairing_page);
    lv_obj_set_style_pad_all(s_pairing_page, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pairing_page, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_pairing_page, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pairing_page, lv_color_hex(0x07111f), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(s_pairing_page, lv_color_hex(0x0c2940), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(s_pairing_page, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_clear_flag(s_pairing_page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *brand = make_text(s_pairing_page, "光衡能源伙伴", 0xffffff,
                                &gh_font_chinese_16);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 20, 18);
    lv_obj_t *secure = make_text(s_pairing_page, "安全连接", 0x35d6a0,
                                 &gh_font_chinese_14);
    lv_obj_align(secure, LV_ALIGN_TOP_RIGHT, -20, 20);

    lv_obj_t *halo = lv_obj_create(s_pairing_page);
    lv_obj_set_size(halo, 92, 92);
    lv_obj_align(halo, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(halo, lv_color_hex(0x143d59), LV_PART_MAIN);
    lv_obj_set_style_border_color(halo, lv_color_hex(0x28c6ef), LV_PART_MAIN);
    lv_obj_set_style_border_width(halo, 2, LV_PART_MAIN);
    lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *link_mark = make_text(halo, "GH", 0x7ee8ff, &lv_font_montserrat_24);
    lv_obj_center(link_mark);

    lv_obj_t *title = make_text(s_pairing_page, "配对设备", 0xffffff,
                                &gh_font_chinese_16);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 164);
    lv_obj_t *hint = make_text(s_pairing_page, "在光衡 App 中输入下方配对码", 0x91a9bf,
                               &gh_font_chinese_14);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 194);

    lv_obj_t *code_card = lv_obj_create(s_pairing_page);
    lv_obj_set_size(code_card, 332, 82);
    lv_obj_align(code_card, LV_ALIGN_TOP_MID, 0, 228);
    lv_obj_set_style_bg_color(code_card, lv_color_hex(0x0c1725), LV_PART_MAIN);
    lv_obj_set_style_border_color(code_card, lv_color_hex(0x31516d), LV_PART_MAIN);
    lv_obj_set_style_border_width(code_card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(code_card, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_all(code_card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(code_card, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 6; ++i) {
        lv_obj_t *digit_box = lv_obj_create(code_card);
        lv_obj_set_size(digit_box, 44, 54);
        lv_obj_set_pos(digit_box, 13 + i * 52, 14);
        lv_obj_set_style_bg_color(digit_box, lv_color_hex(0x172b40), LV_PART_MAIN);
        lv_obj_set_style_border_width(digit_box, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(digit_box, 13, LV_PART_MAIN);
        lv_obj_set_style_pad_all(digit_box, 0, LV_PART_MAIN);
        lv_obj_clear_flag(digit_box, LV_OBJ_FLAG_SCROLLABLE);
        s_pairing_digits[i] = make_text(digit_box, "-", 0xffffff, &lv_font_montserrat_28);
        lv_obj_center(s_pairing_digits[i]);
    }

    lv_obj_t *waiting = make_text(s_pairing_page, "等待手机确认", 0xffb84b,
                                  &gh_font_chinese_16);
    lv_obj_align(waiting, LV_ALIGN_TOP_MID, 0, 332);
    lv_obj_t *privacy = make_text(s_pairing_page,
                                  "配对码短时有效 | 不会显示或保存账户密钥",
                                  0x718aa2, &gh_font_chinese_14);
    lv_obj_align(privacy, LV_ALIGN_TOP_MID, 0, 366);
}

static void create_ble_security_page(lv_obj_t *screen)
{
    s_ble_security_page = lv_obj_create(screen);
    lv_obj_set_size(s_ble_security_page, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_ble_security_page);
    lv_obj_set_style_pad_all(s_ble_security_page, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ble_security_page, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ble_security_page, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ble_security_page, lv_color_hex(0x07111f), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(s_ble_security_page, lv_color_hex(0x0c2940), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(s_ble_security_page, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_clear_flag(s_ble_security_page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *brand = make_text(s_ble_security_page, "光衡能源伙伴", 0xffffff,
                                &gh_font_chinese_16);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 20, 22);
    lv_obj_t *mode = make_text(s_ble_security_page, "蓝牙配网", 0x35d6a0,
                               &gh_font_chinese_14);
    lv_obj_align(mode, LV_ALIGN_TOP_RIGHT, -20, 24);

    lv_obj_t *halo = lv_obj_create(s_ble_security_page);
    lv_obj_set_size(halo, 104, 104);
    lv_obj_align(halo, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(halo, lv_color_hex(0x143d59), LV_PART_MAIN);
    lv_obj_set_style_border_color(halo, lv_color_hex(0x35d6a0), LV_PART_MAIN);
    lv_obj_set_style_border_width(halo, 3, LV_PART_MAIN);
    lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *mark = make_text(halo, "BLE", 0x7ee8ff, &lv_font_montserrat_20);
    lv_obj_center(mark);

    lv_obj_t *title = make_text(s_ble_security_page, "确认安全码", 0xffffff,
                                &gh_font_chinese_16);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 210);
    lv_obj_t *hint = make_text(s_ble_security_page, "请在手机蓝牙提示中输入下方号码", 0x91a9bf,
                               &gh_font_chinese_14);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 242);

    lv_obj_t *code_card = lv_obj_create(s_ble_security_page);
    lv_obj_set_size(code_card, 332, 82);
    lv_obj_align(code_card, LV_ALIGN_TOP_MID, 0, 282);
    lv_obj_set_style_bg_color(code_card, lv_color_hex(0x0c1725), LV_PART_MAIN);
    lv_obj_set_style_border_color(code_card, lv_color_hex(0x31516d), LV_PART_MAIN);
    lv_obj_set_style_border_width(code_card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(code_card, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_all(code_card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(code_card, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 6; ++i) {
        lv_obj_t *digit_box = lv_obj_create(code_card);
        lv_obj_set_size(digit_box, 44, 54);
        lv_obj_set_pos(digit_box, 13 + i * 52, 14);
        lv_obj_set_style_bg_color(digit_box, lv_color_hex(0x172b40), LV_PART_MAIN);
        lv_obj_set_style_border_width(digit_box, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(digit_box, 13, LV_PART_MAIN);
        lv_obj_set_style_pad_all(digit_box, 0, LV_PART_MAIN);
        lv_obj_clear_flag(digit_box, LV_OBJ_FLAG_SCROLLABLE);
        s_ble_security_digits[i] = make_text(digit_box, "-", 0xffffff,
                                             &lv_font_montserrat_28);
        lv_obj_center(s_ble_security_digits[i]);
    }
    lv_obj_t *privacy = make_text(s_ble_security_page,
                                  "仅用于本次加密连接 | 不会通过日志输出",
                                  0x718aa2, &gh_font_chinese_14);
    lv_obj_align(privacy, LV_ALIGN_TOP_MID, 0, 390);
}

static void hold_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_hold_elapsed_ms += 50;
    if (s_hold_elapsed_ms < CONFIG_GH_APPROVAL_HOLD_MS) {
        return;
    }
    lv_timer_pause(s_hold_timer);
    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 Approval Hold | "
             "THRESHOLD | action_set_id=%d elapsed_ms=%u",
             s_model != NULL ? s_model->action_set_id : 0,
             (unsigned)s_hold_elapsed_ms);
    if (s_approval_cb != NULL) {
        s_approval_cb(s_approval_context);
    }
}

static void approval_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_hold_elapsed_ms = 0;
        ESP_LOGI(TAG,
                 "ESP32-S3 Approval Hold started; action_set_id=%d required_ms=%d",
                 s_model != NULL ? s_model->action_set_id : 0,
                 CONFIG_GH_APPROVAL_HOLD_MS);
        lv_timer_resume(s_hold_timer);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_hold_elapsed_ms > 0 && s_hold_elapsed_ms < CONFIG_GH_APPROVAL_HOLD_MS) {
            ESP_LOGI(TAG,
                     "ESP32-S3 Approval Hold cancelled; action_set_id=%d elapsed_ms=%u",
                     s_model != NULL ? s_model->action_set_id : 0,
                     (unsigned)s_hold_elapsed_ms);
        }
        lv_timer_pause(s_hold_timer);
        s_hold_elapsed_ms = 0;
    }
}

static lv_obj_t *make_page(lv_obj_t *screen, gh_screen_t route)
{
    lv_obj_t *page = lv_obj_create(screen);
    lv_obj_set_size(page, 368, 448);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x06101e), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(page, lv_color_hex(0x0b2940), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(page, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    s_pages[route] = page;
    return page;
}

static void add_header(lv_obj_t *page, const char *title, uint32_t color)
{
    lv_obj_t *brand = make_text(page, "光衡", 0x8da5bb, &gh_font_chinese_14);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 20, 16);
    lv_obj_t *label = make_text(page, title, color, &gh_font_chinese_16);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 20, 48);
}

static void create_progress_page(lv_obj_t *screen, gh_screen_t route,
                                 const char *mark, const char *title,
                                 const char *subtitle, uint32_t accent)
{
    lv_obj_t *page = make_page(screen, route);
    lv_obj_t *halo = lv_obj_create(page);
    lv_obj_set_size(halo, 142, 142);
    lv_obj_align(halo, LV_ALIGN_CENTER, 0, -42);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(halo, lv_color_hex(0x102a42), LV_PART_MAIN);
    lv_obj_set_style_border_color(halo, lv_color_hex(accent), LV_PART_MAIN);
    lv_obj_set_style_border_width(halo, 3, LV_PART_MAIN);
    lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *symbol = make_text(halo, mark, accent, &lv_font_montserrat_28);
    lv_obj_center(symbol);
    lv_obj_t *heading = make_text(page, title, 0xffffff, &gh_font_chinese_16);
    lv_obj_align(heading, LV_ALIGN_CENTER, 0, 64);
    lv_obj_t *hint = make_text(page, subtitle, 0x91a9bf, &gh_font_chinese_14);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 102);
}

static void swipe_touch_cb(lv_event_t *event)
{
    if (s_model == NULL) return;

    const lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) return;

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &s_swipe_start);
        s_swipe_tracking = true;
        s_swipe_origin = s_model->screen;
        return;
    }

    if (code != LV_EVENT_RELEASED || !s_swipe_tracking) return;

    lv_point_t end;
    lv_indev_get_point(indev, &end);
    s_swipe_tracking = false;
    const int32_t dx = (int32_t)end.x - (int32_t)s_swipe_start.x;
    const int32_t dy = (int32_t)end.y - (int32_t)s_swipe_start.y;
    const int32_t abs_dx = dx < 0 ? -dx : dx;
    const int32_t abs_dy = dy < 0 ? -dy : dy;

    ESP_LOGI(TAG,
             "ESP32-S3 touch swipe origin=%s dx=%ld dy=%ld",
             screen_name(s_swipe_origin), (long)dx, (long)dy);

    /* CST816S reports short coordinate travel on this 368 x 448 panel.  The
     * board may also be worn with the USB connector on either side, which
     * reverses the logical horizontal sign.  Page context therefore decides
     * the destination; the vector only has to be meaningfully horizontal. */
    if (abs_dx < 24 || abs_dx * 100 < abs_dy * 65) return;
    if (s_swipe_origin == GH_SCREEN_AMBIENT) {
        if (s_navigation_cb != NULL) {
            s_navigation_cb(GH_SCREEN_DEVICE_STATUS, s_approval_context);
        }
    } else if (s_swipe_origin == GH_SCREEN_DEVICE_STATUS ||
               s_swipe_origin == GH_SCREEN_VERIFICATION ||
               s_swipe_origin == GH_SCREEN_VOICE_RESULT) {
        if (s_navigation_cb != NULL) {
            s_navigation_cb(GH_SCREEN_AMBIENT, s_approval_context);
        }
    }
}

static void enable_swipe_tracking(lv_obj_t *object)
{
    if (object == NULL) return;
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(object, swipe_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(object, swipe_touch_cb, LV_EVENT_RELEASED, NULL);
    const uint32_t children = lv_obj_get_child_count(object);
    for (uint32_t i = 0; i < children; ++i) {
        enable_swipe_tracking(lv_obj_get_child(object, (int32_t)i));
    }
}

static const char *screen_name(gh_screen_t route)
{
    static const char *names[GH_SCREEN_COUNT] = {
        "BOOT", "WIFI_CONNECTING", "TIME_SYNC", "BACKEND_CONNECTING",
        "PAIRING", "AMBIENT", "VOICE_LISTENING", "VOICE_TRANSCRIBING",
        "VOICE_RESULT", "EXPLAIN", "PROPOSAL", "APPROVAL",
        "EXECUTION", "VERIFICATION", "OFFLINE", "DEVICE_STATUS"
    };
    return route < GH_SCREEN_COUNT ? names[route] : "UNKNOWN";
}

static void show_screen(gh_screen_t route)
{
    if (route >= GH_SCREEN_COUNT || s_pages[route] == NULL) return;
    if (s_visible_screen < GH_SCREEN_COUNT &&
        s_visible_screen != route &&
        s_pages[s_visible_screen] != NULL) {
        lv_obj_add_flag(s_pages[s_visible_screen], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_pages[route], LV_OBJ_FLAG_HIDDEN);
    /* Pages are siblings created once and every non-active page remains
     * hidden.  Reordering the complete page tree here needlessly invalidated
     * rounded descendants while the LVGL software renderer was processing the
     * release event.  Only the old and new pages now change visibility. */
    lv_obj_set_y(s_pages[route], 0);
    lv_obj_invalidate(s_pages[route]);
    s_visible_screen = route;
    ESP_LOGI(TAG, "ESP32-S3 UI route=%s canvas=368x448", screen_name(route));
}

static void proposal_open_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_navigation_cb == NULL) return;
    s_navigation_cb(GH_SCREEN_APPROVAL, s_approval_context);
}

static void approval_back_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_navigation_cb == NULL) return;
    s_navigation_cb(GH_SCREEN_PROPOSAL, s_approval_context);
}

static void back_to_ambient_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_navigation_cb == NULL) return;
    s_navigation_cb(GH_SCREEN_AMBIENT, s_approval_context);
}

static void voice_button_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_voice_cb == NULL) return;
    s_voice_cb(s_approval_context);
}

static void create_ambient_page(lv_obj_t *screen)
{
    lv_obj_t *page = make_page(screen, GH_SCREEN_AMBIENT);
    add_header(page, "家庭能源", 0xffffff);
    s_source = make_text(page, "--", 0x53d4ab, &gh_font_chinese_14);
    lv_obj_align(s_source, LV_ALIGN_TOP_RIGHT, -20, 18);
    lv_obj_t *halo = lv_obj_create(page);
    lv_obj_set_size(halo, 226, 226);
    lv_obj_align(halo, LV_ALIGN_TOP_MID, 0, 91);
    /* A 226 px circular object with a nested circular button triggered an
     * LVGL 9.6 software radius-mask LoadProhibited on this ESP32-S3 display
     * when the page was first revealed.  Keep the same visual hierarchy with
     * a watch-style energy card that does not require a full-circle mask. */
    lv_obj_set_style_radius(halo, 32, LV_PART_MAIN);
    lv_obj_set_style_bg_color(halo, lv_color_hex(0x0d263e), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(halo, lv_color_hex(0x123f53), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(halo, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(halo, 0, LV_PART_MAIN);
    lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
    s_primary_label = make_text(halo, "等待能源数据", 0x9fb8cb, &gh_font_chinese_14);
    lv_obj_align(s_primary_label, LV_ALIGN_CENTER, 0, -32);
    s_primary_value = make_text(halo, "--", 0xffffff, &lv_font_montserrat_28);
    lv_obj_align(s_primary_value, LV_ALIGN_CENTER, 0, 12);
    s_stale = make_text(halo, "", 0xaab7c3, &gh_font_chinese_14);
    lv_obj_align(s_stale, LV_ALIGN_CENTER, 0, 57);
    lv_obj_t *voice = lv_obj_create(halo);
    lv_obj_remove_style_all(voice);
    lv_obj_add_flag(voice, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(voice, 58, 58);
    lv_obj_align(voice, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_radius(voice, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(voice, lv_color_hex(0x247be8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(voice, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(voice, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(voice, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(voice, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(voice, voice_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *voice_label = make_text(voice, "语音", 0xffffff, &gh_font_chinese_14);
    lv_obj_center(voice_label);
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *card = lv_obj_create(page);
        lv_obj_set_size(card, 104, 72);
        lv_obj_set_pos(card, 16 + i * 116, 354);
        lv_obj_set_style_radius(card, 18, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x10243a), LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        s_secondary[i] = make_text(card, "--", 0xdce9f3, &gh_font_chinese_14);
        lv_obj_center(s_secondary[i]);
    }
}

static void create_voice_pages(lv_obj_t *screen)
{
    lv_obj_t *listening = make_page(screen, GH_SCREEN_VOICE_LISTENING);
    add_header(listening, "语音助手", 0xffffff);
    lv_obj_t *wave = lv_obj_create(listening);
    lv_obj_set_size(wave, 262, 230);
    lv_obj_align(wave, LV_ALIGN_CENTER, 0, -18);
    lv_obj_set_style_radius(wave, 28, LV_PART_MAIN);
    lv_obj_set_style_bg_color(wave, lv_color_hex(0x0b3448), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(wave, lv_color_hex(0x0b1e3d), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(wave, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(wave, 0, LV_PART_MAIN);
    lv_obj_clear_flag(wave, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 5; ++i) {
        s_voice_bars[i] = lv_obj_create(wave);
        lv_obj_set_size(s_voice_bars[i], 16, 28);
        lv_obj_set_pos(s_voice_bars[i], 68 + i * 27, 102);
        lv_obj_set_style_radius(s_voice_bars[i], 8, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_voice_bars[i], lv_color_hex(0x49dfcd), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_voice_bars[i], 0, LV_PART_MAIN);
    }
    s_voice_status = make_text(listening, "正在聆听", 0xffffff, &gh_font_chinese_16);
    lv_obj_align(s_voice_status, LV_ALIGN_BOTTOM_MID, 0, -68);
    lv_obj_t *stop = lv_obj_create(listening);
    lv_obj_add_flag(stop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(stop, 146, 42);
    lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_radius(stop, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(stop, voice_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stop_label = make_text(stop, "停止聆听", 0xffffff, &gh_font_chinese_14);
    lv_obj_center(stop_label);

    create_progress_page(screen, GH_SCREEN_VOICE_TRANSCRIBING, "...", "正在识别",
                         "真实语音正在安全处理", 0x39c9f0);

    lv_obj_t *result = make_page(screen, GH_SCREEN_VOICE_RESULT);
    add_header(result, "光衡回答", 0x58d6a5);
    lv_obj_t *card = make_card(result, 84, 344);
    /* Voice answers can be longer than one watch-sized viewport.  Keep the
     * header fixed and make only the answer card vertically scrollable, so a
     * vertical drag reads the complete response while a horizontal swipe is
     * still reserved for returning to the household-energy page. */
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(card, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 16, LV_PART_MAIN);
    s_voice_intent = make_text(card, "", 0x58d6a5, &gh_font_chinese_14);
    lv_obj_set_width(s_voice_intent, 294);
    s_voice_transcript = make_text(card, "", 0x91a9bd, &gh_font_chinese_14);
    lv_label_set_long_mode(s_voice_transcript, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_voice_transcript, 294);
    s_voice_answer = make_text(card, "", 0xffffff, &gh_font_chinese_16);
    lv_label_set_long_mode(s_voice_answer, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_voice_answer, 294);
}

static void create_execution_pages(lv_obj_t *screen)
{
    lv_obj_t *execution = make_page(screen, GH_SCREEN_EXECUTION);
    add_header(execution, "分步执行", 0xffbc59);
    for (int i = 0; i < GH_MAX_ACTIONS; ++i) {
        lv_obj_t *card = lv_obj_create(execution);
        lv_obj_set_size(card, 328, 62);
        lv_obj_set_pos(card, 20, 82 + i * 70);
        lv_obj_set_style_radius(card, 17, LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x10243a), LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        s_execution_rows[i] = make_text(card, "等待后端状态", 0xdce9f3,
                                        &gh_font_chinese_14);
        lv_label_set_long_mode(s_execution_rows[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_execution_rows[i], 292);
        lv_obj_center(s_execution_rows[i]);
    }
    lv_obj_t *hint = make_text(execution, "执行进度以设备回读为准", 0x718aa2,
                               &gh_font_chinese_14);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -18);

    lv_obj_t *verification = make_page(screen, GH_SCREEN_VERIFICATION);
    add_header(verification, "总表验证", 0x58d6a5);

    lv_obj_t *outcome = lv_obj_create(verification);
    lv_obj_set_size(outcome, 328, 134);
    lv_obj_set_pos(outcome, 20, 78);
    lv_obj_set_style_radius(outcome, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(outcome, lv_color_hex(0x0d2638), LV_PART_MAIN);
    lv_obj_set_style_border_color(outcome, lv_color_hex(0x1d5264), LV_PART_MAIN);
    lv_obj_set_style_border_width(outcome, 1, LV_PART_MAIN);
    lv_obj_clear_flag(outcome, LV_OBJ_FLAG_SCROLLABLE);
    s_verification_status = make_text(outcome, "等待总表验证", 0xffbc59,
                                      &gh_font_chinese_16);
    lv_obj_align(s_verification_status, LV_ALIGN_TOP_LEFT, 0, 0);
    s_verification_power = make_text(outcome, "电网功率 -- W  到  -- W", 0xffffff,
                                     &gh_font_chinese_16);
    lv_obj_align(s_verification_power, LV_ALIGN_TOP_LEFT, 0, 38);
    s_verification_summary = make_text(outcome, "等待 Smart Meter 真实回读",
                                       0x91a9bd, &gh_font_chinese_14);
    lv_label_set_long_mode(s_verification_summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_verification_summary, 292);
    lv_obj_align(s_verification_summary, LV_ALIGN_TOP_LEFT, 0, 76);

    const char *levels[3] = {"L1  命令接收", "L2  设备回读", "L3  家庭效果"};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *card = lv_obj_create(verification);
        lv_obj_set_size(card, 328, 48);
        lv_obj_set_pos(card, 20, 222 + i * 54);
        lv_obj_set_style_radius(card, 15, LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x10243a), LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        s_verification_cards[i] = card;
        s_verification_levels[i] = make_text(card, levels[i], 0xa8bbca,
                                             &gh_font_chinese_14);
        lv_obj_center(s_verification_levels[i]);
    }
    lv_obj_t *verification_hint =
        make_text(verification, "左右滑动返回家庭能源", 0x718aa2,
                  &gh_font_chinese_14);
    lv_obj_align(verification_hint, LV_ALIGN_BOTTOM_MID, 0, -10);
}

static void create_explain_page(lv_obj_t *screen)
{
    lv_obj_t *page = make_page(screen, GH_SCREEN_EXPLAIN);
    add_header(page, "为什么现在？", 0xffbc59);
    lv_obj_t *card = make_card(page, 104, 246);
    s_explain_body = make_text(card, "暂无可解释的能源事实", 0xffffff,
                               &gh_font_chinese_16);
    lv_label_set_long_mode(s_explain_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_explain_body, 294);
    lv_obj_align(s_explain_body, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_obj_t *back = lv_obj_create(page);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(back, 142, 44);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_style_radius(back, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(back, back_to_ambient_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = make_text(back, "返回能源", 0xffffff, &gh_font_chinese_14);
    lv_obj_center(label);
}

static void create_proposal_pages(lv_obj_t *screen)
{
    lv_obj_t *proposal = make_page(screen, GH_SCREEN_PROPOSAL);
    add_header(proposal, "智能建议", 0xffbc59);
    lv_obj_t *card = make_card(proposal, 92, 280);
    s_proposal_title = make_text(card, "等待建议", 0xffffff, &gh_font_chinese_16);
    lv_obj_align(s_proposal_title, LV_ALIGN_TOP_LEFT, 0, 4);
    s_proposal_reason = make_text(card, "", 0xaec1d2, &gh_font_chinese_14);
    lv_label_set_long_mode(s_proposal_reason, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_proposal_reason, 294);
    lv_obj_align(s_proposal_reason, LV_ALIGN_TOP_LEFT, 0, 44);
    s_proposal_impact = make_text(card, "", 0x56d6a4, &gh_font_chinese_16);
    lv_obj_align(s_proposal_impact, LV_ALIGN_TOP_LEFT, 0, 132);
    s_proposal_count = make_text(card, "", 0x8eb8df, &gh_font_chinese_14);
    lv_obj_align(s_proposal_count, LV_ALIGN_TOP_LEFT, 0, 174);
    lv_obj_t *open = lv_obj_create(card);
    lv_obj_add_flag(open, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(open, 294, 46);
    lv_obj_align(open, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(open, 17, LV_PART_MAIN);
    lv_obj_set_style_bg_color(open, lv_color_hex(0x246fe8), LV_PART_MAIN);
    lv_obj_add_event_cb(open, proposal_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *open_label = make_text(open, "查看并确认", 0xffffff, &gh_font_chinese_14);
    lv_obj_center(open_label);

    lv_obj_t *approval = make_page(screen, GH_SCREEN_APPROVAL);
    add_header(approval, "确认执行", 0xffffff);
    lv_obj_t *approval_card = make_card(approval, 100, 238);
    lv_obj_t *notice = make_text(approval_card, "请确认本次跨设备调整", 0xffbc59,
                                 &gh_font_chinese_14);
    lv_obj_align(notice, LV_ALIGN_TOP_LEFT, 0, 4);
    s_approval_impact = make_text(approval_card, "", 0xffffff, &gh_font_chinese_16);
    lv_label_set_long_mode(s_approval_impact, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_approval_impact, 294);
    lv_obj_align(s_approval_impact, LV_ALIGN_TOP_LEFT, 0, 48);
    s_approve_button = lv_obj_create(approval_card);
    lv_obj_add_flag(s_approve_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_approve_button, 294, 54);
    lv_obj_align(s_approve_button, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_approve_button, lv_color_hex(0x216fed), LV_PART_MAIN);
    lv_obj_set_style_radius(s_approve_button, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(s_approve_button, approval_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_t *approve_label = make_text(s_approve_button, "长按确认", 0xffffff,
                                        &gh_font_chinese_16);
    lv_obj_center(approve_label);
    lv_obj_t *back = lv_obj_create(approval);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(back, 110, 40);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_style_radius(back, 16, LV_PART_MAIN);
    lv_obj_add_event_cb(back, approval_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = make_text(back, "返回建议", 0xffffff, &gh_font_chinese_14);
    lv_obj_center(back_label);
}

static void create_device_page(lv_obj_t *screen)
{
    lv_obj_t *page = make_page(screen, GH_SCREEN_DEVICE_STATUS);
    add_header(page, "终端状态", 0xffffff);
    const char *names[5] = {"Wi-Fi", "安全连接", "时间", "云端", "终端电量"};
    for (int i = 0; i < 5; ++i) {
        lv_obj_t *card = lv_obj_create(page);
        lv_obj_set_size(card, 328, 52);
        lv_obj_set_pos(card, 20, 92 + i * 60);
        lv_obj_set_style_radius(card, 16, LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x10243a), LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *name = make_text(card, names[i], 0xb5c7d6, &gh_font_chinese_14);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 4, 0);
        s_device_rows[i] = make_text(card, "--", 0x55d7a4, &gh_font_chinese_14);
        lv_obj_align(s_device_rows[i], LV_ALIGN_RIGHT_MID, -4, 0);
    }
    lv_obj_t *hint = make_text(page, "向右滑动返回", 0x718aa2, &gh_font_chinese_14);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);
}

static void create_offline_page(lv_obj_t *screen)
{
    lv_obj_t *page = make_page(screen, GH_SCREEN_OFFLINE);
    add_header(page, "连接已中断", 0xff6f72);
    lv_obj_t *halo = lv_obj_create(page);
    lv_obj_set_size(halo, 170, 170);
    lv_obj_align(halo, LV_ALIGN_CENTER, 0, -38);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(halo, lv_color_hex(0x252b39), LV_PART_MAIN);
    lv_obj_set_style_border_color(halo, lv_color_hex(0x58677a), LV_PART_MAIN);
    lv_obj_set_style_border_width(halo, 2, LV_PART_MAIN);
    lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *mark = make_text(halo, "--", 0x9cadbb, &lv_font_montserrat_28);
    lv_obj_center(mark);
    s_offline_sync = make_text(page, "正在重新连接...", 0xaebdca, &gh_font_chinese_14);
    lv_obj_align(s_offline_sync, LV_ALIGN_CENTER, 0, 86);
}

static void create_ui(void)
{
    lv_obj_t *screen = active_screen();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x06101e), LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, screen_touch_event_cb, LV_EVENT_PRESSED, NULL);
    create_progress_page(screen, GH_SCREEN_BOOT, "GH", "光衡", "启动中...", 0x39c9f0);
    create_progress_page(screen, GH_SCREEN_WIFI_CONNECTING, "WiFi", "连接网络",
                         "正在连接家庭网络...", 0x5aa8ff);
    create_progress_page(screen, GH_SCREEN_TIME_SYNC, "TIME", "同步时间",
                         "正在同步系统时间...", 0x58d6a5);
    create_progress_page(screen, GH_SCREEN_BACKEND_CONNECTING, "GH", "连接光衡",
                         "正在建立安全连接...", 0x58d6a5);
    create_ambient_page(screen);
    create_voice_pages(screen);
    create_explain_page(screen);
    create_proposal_pages(screen);
    create_execution_pages(screen);
    create_offline_page(screen);
    create_device_page(screen);
    enable_swipe_tracking(s_pages[GH_SCREEN_AMBIENT]);
    enable_swipe_tracking(s_pages[GH_SCREEN_DEVICE_STATUS]);
    /* A completed Action Set is historical information, not a modal trap.
     * Swiping away acknowledges only this displayed Action Set id, so the
     * next Snapshot does not force the same verification page back on top. */
    enable_swipe_tracking(s_pages[GH_SCREEN_VERIFICATION]);
    enable_swipe_tracking(s_pages[GH_SCREEN_VOICE_RESULT]);
    create_pairing_page(screen);
    s_pages[GH_SCREEN_PAIRING] = s_pairing_page;
    lv_obj_add_flag(s_pairing_page, LV_OBJ_FLAG_HIDDEN);
    create_ble_security_page(screen);
    lv_obj_add_flag(s_ble_security_page, LV_OBJ_FLAG_HIDDEN);
    s_hold_timer = lv_timer_create(hold_timer_cb, 50, NULL);
    lv_timer_pause(s_hold_timer);
}

typedef enum {
    GH_VERIFY_WAITING = 0,
    GH_VERIFY_PASSED,
    GH_VERIFY_PARTIAL,
    GH_VERIFY_FAILED,
} gh_verify_row_state_t;

static void set_verification_row(size_t index, const char *text,
                                 gh_verify_row_state_t state)
{
    if (index >= 3 || s_verification_cards[index] == NULL ||
        s_verification_levels[index] == NULL) {
        return;
    }
    uint32_t background = 0x10243a;
    uint32_t foreground = 0xa8bbca;
    if (state == GH_VERIFY_PASSED) {
        background = 0x0d302b;
        foreground = 0x58d6a5;
    } else if (state == GH_VERIFY_PARTIAL) {
        background = 0x342817;
        foreground = 0xffbc59;
    } else if (state == GH_VERIFY_FAILED) {
        background = 0x351c27;
        foreground = 0xff7b7f;
    }
    lv_obj_set_style_bg_color(s_verification_cards[index],
                              lv_color_hex(background), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_verification_levels[index],
                                lv_color_hex(foreground), LV_PART_MAIN);
    lv_label_set_text(s_verification_levels[index], text);
}

esp_err_t gh_ui_start(gh_app_model_t *model, gh_ui_approval_cb_t approval_cb,
                      gh_ui_voice_cb_t voice_cb,
                      gh_ui_navigation_cb_t navigation_cb, void *context)
{
    if (model == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_model = model;
    s_approval_cb = approval_cb;
    s_voice_cb = voice_cb;
    s_navigation_cb = navigation_cb;
    s_approval_context = context;

    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        return ESP_FAIL;
    }
    esp_err_t board_result = gh_hardware_report_board_variant();
    if (board_result != ESP_OK) {
        ESP_LOGW(TAG, "ESP32-S3 board revision remains unverified: %s",
                 esp_err_to_name(board_result));
    }
    ESP_RETURN_ON_ERROR(bsp_display_brightness_set(100), TAG, "brightness failed");
    if (!bsp_display_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }
    create_ui();
    bsp_display_unlock();

    /* Render outside the creation lock: gh_ui_render() owns the same LVGL
     * mutex, so nesting it prevented the initial live labels from rendering. */
    gh_ui_render(model);
    if (!bsp_display_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }
    lv_obj_invalidate(active_screen());
    lv_refr_now(display);
    bsp_display_unlock();
    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 AMOLED First Frame | "
             "PASS | controller=CO5300 brightness=100");
    return ESP_OK;
}

void gh_ui_render(const gh_app_model_t *model)
{
    if (model == NULL || s_pages[GH_SCREEN_BOOT] == NULL || !bsp_display_lock(100)) {
        return;
    }
    char buffer[256];
    lv_label_set_text(s_source, source_mode_label(model->energy.source_mode));
    const uint32_t scaled = model->voice_rms > 16000 ? 16000 : model->voice_rms;
    for (int i = 0; i < 5; ++i) {
        int height = 24 + (int)(scaled * (35 + (i % 3) * 10) / 16000);
        lv_obj_set_height(s_voice_bars[i], height);
        lv_obj_set_y(s_voice_bars[i], 116 - height / 2);
    }
    lv_label_set_text(s_voice_status,
                      model->voice_speech ? "正在聆听你的声音" : "正在聆听");
    snprintf(buffer, sizeof(buffer), "意图：%s",
             model->voice_intent[0] ? model->voice_intent : "UNKNOWN");
    lv_label_set_text(s_voice_intent, buffer);
    snprintf(buffer, sizeof(buffer), "你说：%s",
             model->voice_transcript[0] ? model->voice_transcript : "--");
    lv_label_set_text(s_voice_transcript, buffer);
    lv_label_set_text(s_voice_answer,
                      model->voice_state == GH_VOICE_FAILED
                          ? model->voice_error
                          : (model->voice_answer[0] ? model->voice_answer
                                                   : "暂时无法生成可靠回答。"));
    for (int i = 0; i < 6; ++i) {
        char digit[2] = {'-', '\0'};
        if (model->pairing_code[i] >= '0' && model->pairing_code[i] <= '9') {
            digit[0] = model->pairing_code[i];
        }
        lv_label_set_text(s_pairing_digits[i], digit);
    }

    const char *primary = "能源状态";
    float primary_kw = model->energy.home_kw;
    if (model->energy.grid_kw > 0.01f && model->energy.grid_importing) {
        primary = "正在购电";
        primary_kw = model->energy.grid_kw;
    } else if (model->energy.grid_kw > 0.01f) {
        primary = "正在反送";
        primary_kw = model->energy.grid_kw;
    } else if (model->energy.battery_charging) {
        primary = "储能充电";
    } else if (model->energy.pv_kw > model->energy.home_kw) {
        primary = "光伏盈余";
        primary_kw = model->energy.pv_kw - model->energy.home_kw;
    } else if (model->energy.pv_kw > 0.01f) {
        primary = "正在使用光伏";
        primary_kw = model->energy.pv_kw;
    } else {
        primary = "家庭用电";
    }
    lv_label_set_text(s_primary_label, primary);
    snprintf(buffer, sizeof(buffer), "%.1f kW", primary_kw);
    lv_label_set_text(s_primary_value, buffer);
    snprintf(buffer, sizeof(buffer), "光伏\n%.1f kW", model->energy.pv_kw);
    lv_label_set_text(s_secondary[0], buffer);
    snprintf(buffer, sizeof(buffer), "家庭\n%.1f kW", model->energy.home_kw);
    lv_label_set_text(s_secondary[1], buffer);
    snprintf(buffer, sizeof(buffer), "储能\n%.0f%%", model->energy.battery_soc_pct);
    lv_label_set_text(s_secondary[2], buffer);
    if (model->energy.stale) {
        if (model->energy.last_successful_sync_at > 0) {
            time_t timestamp = (time_t)model->energy.last_successful_sync_at;
            struct tm local = {0};
            localtime_r(&timestamp, &local);
            snprintf(buffer, sizeof(buffer), "数据已过期  %02d:%02d", local.tm_hour,
                     local.tm_min);
        } else {
            snprintf(buffer, sizeof(buffer), "数据已过期");
        }
        lv_label_set_text(s_stale, buffer);
    } else {
        lv_label_set_text(s_stale, "");
    }

    if (model->energy.grid_kw > 0.01f && !model->energy.grid_importing) {
        snprintf(buffer, sizeof(buffer),
                 "光伏发电 %.1f kW\n家庭使用 %.1f kW\n\n因此当前约有 %.1f kW\n正在反送电网。",
                 model->energy.pv_kw, model->energy.home_kw, model->energy.grid_kw);
    } else if (model->energy.grid_kw > 0.01f && model->energy.grid_importing) {
        snprintf(buffer, sizeof(buffer),
                 "家庭负载 %.1f kW\n当前从电网购电 %.1f kW\n\n以上为当前能源快照的\n确定性关系。",
                 model->energy.home_kw, model->energy.grid_kw);
    } else {
        snprintf(buffer, sizeof(buffer),
                 "光伏发电 %.1f kW\n家庭使用 %.1f kW\n储能电量 %.0f%%\n\n当前没有明显电网流动。",
                 model->energy.pv_kw, model->energy.home_kw,
                 model->energy.battery_soc_pct);
    }
    lv_label_set_text(s_explain_body, buffer);

    lv_label_set_text(s_proposal_title,
                      model->proposal.title[0] ? model->proposal.title : "智能建议");
    lv_label_set_text(s_proposal_reason,
                      model->proposal.reason[0] ? model->proposal.reason : "暂无待确认建议");
    if (model->proposal.expected_grid_delta_available) {
        snprintf(buffer, sizeof(buffer), "预计减少电网流动 %.1f kW",
                 model->proposal.expected_grid_delta_w / 1000.0f);
    } else {
        snprintf(buffer, sizeof(buffer), "等待影响评估");
    }
    lv_label_set_text(s_proposal_impact, buffer);
    snprintf(buffer, sizeof(buffer), "影响 %u 项设备设置",
             (unsigned)model->proposal.action_count);
    lv_label_set_text(s_proposal_count, buffer);
    snprintf(buffer, sizeof(buffer), "将调整 %u 项设备设置\n%s",
             (unsigned)model->proposal.action_count,
             model->proposal.expected_grid_delta_available ? "执行后将由真实回读验证" :
             "当前没有可靠影响估算");
    lv_label_set_text(s_approval_impact, buffer);
    for (size_t i = 0; i < GH_MAX_ACTIONS; ++i) {
        if (i < model->proposal.action_count) {
            const gh_action_step_t *step = &model->proposal.actions[i];
            const char *state = "等待";
            if (strcmp(step->status, "EXECUTING") == 0) state = "执行中";
            else if (strcmp(step->status, "SUCCEEDED") == 0) state = "已回读";
            else if (strcmp(step->status, "FAILED") == 0) state = "失败";
            else if (strcmp(step->status, "BLOCKED") == 0) state = "已阻断";
            else if (strcmp(step->status, "SKIPPED") == 0) state = "已跳过";
            snprintf(buffer, sizeof(buffer), "%u  %s\n%s  ·  %s",
                     (unsigned)(i + 1), step->device_name, step->capability, state);
        } else {
            snprintf(buffer, sizeof(buffer), "--");
        }
        lv_label_set_text(s_execution_rows[i], buffer);
    }
    const size_t total_actions = model->proposal.action_count;
    size_t accepted_actions = 0;
    size_t readback_actions = 0;
    size_t failed_actions = 0;
    for (size_t i = 0; i < model->proposal.action_count; ++i) {
        const gh_action_step_t *step = &model->proposal.actions[i];
        if (step->execution_id > 0) accepted_actions++;
        if (strcmp(step->status, "SUCCEEDED") == 0 &&
            strcmp(step->result_code, "READBACK_VERIFIED") == 0) {
            readback_actions++;
        }
        if (strcmp(step->status, "FAILED") == 0 ||
            strcmp(step->status, "BLOCKED") == 0 ||
            strcmp(step->status, "SKIPPED") == 0) {
            failed_actions++;
        }
    }

    const bool action_set_partial =
        strcmp(model->proposal.status, "PARTIAL") == 0 ||
        (failed_actions > 0 && readback_actions > 0);
    const bool action_set_blocked =
        strcmp(model->proposal.status, "BLOCKED") == 0;
    const bool level1 = total_actions > 0 && accepted_actions == total_actions;
    const bool level2 = total_actions > 0 && readback_actions == total_actions;
    /* A PARTIAL Action Set can never be presented as fully verified, even if
     * a contradictory upstream payload contains verification_status=VERIFIED. */
    const bool level3 = !action_set_partial && !action_set_blocked &&
        strcmp(model->proposal.status, "SUCCEEDED") == 0 &&
        strcmp(model->proposal.verification_status, "VERIFIED") == 0;

    if (level1) {
        set_verification_row(0, "L1  命令接收    已完成", GH_VERIFY_PASSED);
    } else if (accepted_actions > 0) {
        set_verification_row(0, "L1  命令接收    部分完成", GH_VERIFY_PARTIAL);
    } else {
        set_verification_row(0, "L1  命令接收    未完成", GH_VERIFY_WAITING);
    }

    if (level2) {
        set_verification_row(1, "L2  设备回读    已完成", GH_VERIFY_PASSED);
    } else if (readback_actions > 0) {
        set_verification_row(1, "L2  设备回读    部分完成", GH_VERIFY_PARTIAL);
    } else if (failed_actions > 0) {
        set_verification_row(1, "L2  设备回读    未通过", GH_VERIFY_FAILED);
    } else {
        set_verification_row(1, "L2  设备回读    未完成", GH_VERIFY_WAITING);
    }

    if (level3) {
        set_verification_row(2, "L3  家庭效果    已验证", GH_VERIFY_PASSED);
    } else if (strcmp(model->proposal.verification_status, "FAILED") == 0 ||
               strcmp(model->proposal.verification_status, "NOT_VERIFIED") == 0) {
        set_verification_row(2, "L3  家庭效果    未验证", GH_VERIFY_FAILED);
    } else if (strcmp(model->proposal.verification_status, "UNAVAILABLE") == 0) {
        set_verification_row(2, "L3  家庭效果    数据不可用", GH_VERIFY_FAILED);
    } else {
        set_verification_row(2, "L3  家庭效果    待验证", GH_VERIFY_WAITING);
    }

    const char *outcome_text = "等待总表验证";
    uint32_t outcome_color = 0xffbc59;
    if (action_set_partial) {
        outcome_text = "部分完成";
    } else if (action_set_blocked ||
               (total_actions > 0 && failed_actions == total_actions)) {
        outcome_text = "执行未完成";
        outcome_color = 0xff7b7f;
    } else if (level3) {
        outcome_text = "已验证";
        outcome_color = 0x58d6a5;
    } else if (level2) {
        outcome_text = "设备已调整";
    }
    lv_label_set_text(s_verification_status, outcome_text);
    lv_obj_set_style_text_color(s_verification_status,
                                lv_color_hex(outcome_color), LV_PART_MAIN);

    if (model->proposal.before_grid_available &&
        model->proposal.after_grid_available) {
        snprintf(buffer, sizeof(buffer),
                 "电网功率  %.0f W  到  %.0f W",
                 model->proposal.before_grid_power_w,
                 model->proposal.after_grid_power_w);
    } else {
        snprintf(buffer, sizeof(buffer), "电网功率  -- W  到  -- W");
    }
    lv_label_set_text(s_verification_power, buffer);

    if (level2 && !level3) {
        snprintf(buffer, sizeof(buffer), "家庭能源效果未验证%s%s",
                 model->proposal.verification_message[0] ? "：" : "",
                 model->proposal.verification_message);
    } else {
        snprintf(buffer, sizeof(buffer), "%s",
                 model->proposal.verification_message[0]
                     ? model->proposal.verification_message
                     : "等待 Smart Meter 真实回读");
    }
    lv_label_set_text(s_verification_summary, buffer);

    if (gh_model_can_approve(model)) {
        lv_obj_clear_state(s_approve_button, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s_approve_button, LV_STATE_DISABLED);
    }
    lv_label_set_text(s_device_rows[0],
                      model->connection_state == GH_CONNECTION_CONNECTING ? "连接中" : "已连接");
    lv_label_set_text(s_device_rows[1],
                      model->connection_state == GH_CONNECTION_SECURE_READY ? "正常" : "等待中");
    lv_label_set_text(s_device_rows[2],
                      model->connection_state == GH_CONNECTION_TIME_SYNCING ? "同步中" : "已同步");
    lv_label_set_text(s_device_rows[3], model->backend_online ? "已连接" : "未连接");
    if (model->energy.companion_battery_pct_available) {
        snprintf(buffer, sizeof(buffer), "%.0f%%%s", model->energy.companion_battery_pct,
                 model->energy.companion_battery_charging ? " 充电" : "");
    } else {
        snprintf(buffer, sizeof(buffer), "--");
    }
    lv_label_set_text(s_device_rows[4], buffer);
    if (model->energy.last_successful_sync_at > 0) {
        time_t timestamp = (time_t)model->energy.last_successful_sync_at;
        struct tm local = {0};
        localtime_r(&timestamp, &local);
        snprintf(buffer, sizeof(buffer), "上次同步 %02d:%02d\n正在重新连接...",
                 local.tm_hour, local.tm_min);
        lv_label_set_text(s_offline_sync, buffer);
    } else {
        lv_label_set_text(s_offline_sync, "暂无同步数据\n正在重新连接...");
    }
    if (model->screen < GH_SCREEN_COUNT && model->screen != s_visible_screen) {
        show_screen(model->screen);
    }
    bsp_display_unlock();
}

void gh_ui_show_ble_passkey(uint32_t passkey)
{
    if (s_ble_security_page == NULL || !bsp_display_lock(100)) return;
    char code[7];
    snprintf(code, sizeof(code), "%06lu", (unsigned long)passkey);
    for (int i = 0; i < 6; ++i) {
        char digit[2] = {code[i], '\0'};
        lv_label_set_text(s_ble_security_digits[i], digit);
    }
    lv_obj_clear_flag(s_ble_security_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ble_security_page);
    bsp_display_unlock();
}

void gh_ui_hide_ble_passkey(void)
{
    if (s_ble_security_page == NULL || !bsp_display_lock(100)) return;
    lv_obj_add_flag(s_ble_security_page, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}
