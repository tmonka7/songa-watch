/*
 * 18 - About, 19 - OTA update, 20 - Power off.
 *
 * The parts of the watch that talk about the watch itself.
 */
#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_ota.h"
#include "watch_svc/svc_power.h"
#include "watch_svc/svc_storage.h"
#include "watch_svc/svc_time.h"
#include "watch_svc/svc_vision.h"
#include "watch_svc/svc_wifi.h"

#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "bsp/esp-bsp.h"

/* ============================================================= 18 about == */

static lv_obj_t *s_heap_label;
static lv_obj_t *s_uptime_label;

static void about_tick(lv_timer_t *t)
{
    (void)t;
    char buf[24];

    svc_storage_format_size(esp_get_free_heap_size(), buf, sizeof(buf));
    lv_label_set_text(s_heap_label, buf);

    svc_time_format_uptime(buf, sizeof(buf));
    lv_label_set_text(s_uptime_label, buf);
}

lv_obj_t *scr_about_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_ABOUT, &body);

    /* ---- identity ---- */
    lv_obj_t *head = ui_card(body, 96);

    lv_obj_t *chip_box = lv_obj_create(head);
    ui_style_plain(chip_box);
    lv_obj_set_size(chip_box, 56, 56);
    lv_obj_set_style_radius(chip_box, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(chip_box, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(chip_box, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *chip_ico = lv_label_create(chip_box);
    lv_label_set_text(chip_ico, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(chip_ico, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_center(chip_ico);

    lv_obj_t *name = lv_label_create(head);
    lv_label_set_text(name, "Songa Watch");
    lv_obj_set_style_text_color(name, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(name, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 70, -12);

    lv_obj_t *board = lv_label_create(head);
    lv_label_set_text(board, "ESP32-S3-Touch-AMOLED-2.06");
    lv_obj_set_style_text_color(board, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(board, ui_font(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_align(board, LV_ALIGN_LEFT_MID, 70, 12);

    /* ---- build ---- */
    const esp_app_desc_t *app = esp_app_get_description();
    lv_obj_t *build = ui_card(body, 0);
    lv_obj_set_flex_flow(build, LV_FLEX_FLOW_COLUMN);

    ui_kv_row(build, i18n(STR_VERSION), svc_ota_running_version(), NULL);

    char stamp[40];
    snprintf(stamp, sizeof(stamp), "%s %s", app->date, app->time);
    ui_kv_row(build, i18n(STR_BUILD), stamp, NULL);
    ui_kv_row(build, "IDF", app->idf_ver, NULL);

    /* ---- hardware ---- */
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    lv_obj_t *hw = ui_card(body, 0);
    lv_obj_set_flex_flow(hw, LV_FLEX_FLOW_COLUMN);

    char buf[32];
    snprintf(buf, sizeof(buf), "ESP32-S3 rev%d, %d core",
             (int)chip.revision, (int)chip.cores);
    ui_kv_row(hw, i18n(STR_CHIP), buf, NULL);

    svc_storage_format_size(flash_size, buf, sizeof(buf));
    ui_kv_row(hw, i18n(STR_FLASH), buf, NULL);

    const size_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    svc_storage_format_size(psram, buf, sizeof(buf));
    ui_kv_row(hw, "PSRAM", buf, NULL);

    ui_kv_row(hw, i18n(STR_FREE_HEAP), "-", &s_heap_label);
    ui_kv_row(hw, i18n(STR_UPTIME), "-", &s_uptime_label);

    /* ---- software ---- */
    lv_obj_t *sw = ui_card(body, 0);
    lv_obj_set_flex_flow(sw, LV_FLEX_FLOW_COLUMN);
    snprintf(buf, sizeof(buf), "%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
             LVGL_VERSION_PATCH);
    ui_kv_row(sw, "LVGL", buf, NULL);
    ui_kv_row(sw, i18n(STR_AI_VISION), svc_vision_backend_name(), NULL);

    about_tick(NULL);
    ui_screen_add_timer(scr, about_tick, 1000);
    return scr;
}

/* =============================================================== 19 OTA == */

static lv_obj_t *s_ota_state;
static lv_obj_t *s_ota_detail;
static lv_obj_t *s_ota_bar;
static lv_obj_t *s_ota_pct;
static lv_obj_t *s_ota_primary;
static lv_obj_t *s_ota_primary_label;
static lv_obj_t *s_ota_secondary;

static void ota_refresh(void)
{
    const svc_ota_progress_t *p = svc_ota_progress();

    const char *state_text = i18n(STR_UP_TO_DATE);
    lv_color_t state_color = UI_COLOR_TEXT_DIM;
    const char *action = i18n(STR_CHECKING);
    bool show_bar = false;

    switch (p->state) {
    case SVC_OTA_IDLE:
        state_text = i18n(STR_OTA_UPDATE);
        action = i18n(STR_CHECKING);
        break;
    case SVC_OTA_CHECKING:
        state_text = i18n(STR_CHECKING);
        state_color = UI_COLOR_ACCENT;
        action = i18n(STR_CHECKING);
        break;
    case SVC_OTA_AVAILABLE:
        state_text = i18n(STR_UPDATE_AVAILABLE);
        state_color = UI_COLOR_GREEN;
        action = i18n(STR_DOWNLOAD);
        break;
    case SVC_OTA_UP_TO_DATE:
        state_text = i18n(STR_UP_TO_DATE);
        state_color = UI_COLOR_GREEN;
        action = i18n(STR_CHECKING);
        break;
    case SVC_OTA_DOWNLOADING:
        state_text = i18n(STR_DOWNLOAD);
        state_color = UI_COLOR_ACCENT;
        action = i18n(STR_CANCEL);
        show_bar = true;
        break;
    case SVC_OTA_INSTALLING:
        state_text = i18n(STR_INSTALLING);
        state_color = UI_COLOR_ORANGE;
        action = i18n(STR_INSTALLING);
        show_bar = true;
        break;
    case SVC_OTA_DONE:
        state_text = i18n(STR_REBOOTING);
        state_color = UI_COLOR_GREEN;
        action = i18n(STR_RESTART);
        break;
    case SVC_OTA_FAILED:
        state_text = i18n(STR_UPDATE_FAILED);
        state_color = UI_COLOR_RED;
        action = i18n(STR_RETRY);
        break;
    }

    lv_label_set_text(s_ota_state, state_text);
    lv_obj_set_style_text_color(s_ota_state, state_color, LV_PART_MAIN);
    lv_label_set_text(s_ota_primary_label, action);

    if (p->error[0] != '\0') {
        lv_label_set_text(s_ota_detail, p->error);
    } else if (p->available_version[0] != '\0') {
        /* ASCII arrow: U+2192 is not in Montserrat's glyph set. */
        lv_label_set_text_fmt(s_ota_detail, "%s %s -> %s",
                              i18n(STR_NEW_VERSION), svc_ota_running_version(),
                              p->available_version);
    } else {
        lv_label_set_text_fmt(s_ota_detail, "%s %s", i18n(STR_VERSION),
                              svc_ota_running_version());
    }

    if (show_bar) {
        lv_obj_remove_flag(s_ota_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_ota_pct, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_ota_bar, p->percent, LV_ANIM_ON);
        lv_label_set_text_fmt(s_ota_pct, "%u%%", (unsigned)p->percent);
    } else {
        lv_obj_add_flag(s_ota_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ota_pct, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_ota_primary(lv_event_t *e)
{
    (void)e;
    const svc_ota_progress_t *p = svc_ota_progress();

    switch (p->state) {
    case SVC_OTA_AVAILABLE:
        svc_ota_start();
        break;
    case SVC_OTA_DOWNLOADING:
        svc_ota_abort();
        break;
    case SVC_OTA_DONE:
        svc_ota_reboot();
        break;
    case SVC_OTA_INSTALLING:
        break;   /* not interruptible - do nothing rather than corrupt it */
    default:
        if (svc_wifi_status()->state != SVC_WIFI_CONNECTED) {
            ui_toast(i18n(STR_NEEDS_WIFI), UI_COLOR_ORANGE);
            return;
        }
        svc_ota_check();
        break;
    }
    ota_refresh();
}

static void on_ota_later(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void on_ota_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id != WATCH_EV_OTA_PROGRESS || !ui_nav_is_current(UI_SCR_OTA)) {
        return;
    }
    if (bsp_display_lock(50)) {
        ota_refresh();
        bsp_display_unlock();
    }
}

lv_obj_t *scr_ota_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_OTA_UPDATE, &body);

    lv_obj_t *card = ui_card(body, 210);

    lv_obj_t *cloud = lv_label_create(card);
    lv_label_set_text(cloud, LV_SYMBOL_DOWNLOAD);
    lv_obj_set_style_text_color(cloud, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_text_font(cloud, ui_font(UI_FONT_LARGE), LV_PART_MAIN);
    lv_obj_align(cloud, LV_ALIGN_TOP_MID, 0, 6);

    s_ota_state = lv_label_create(card);
    lv_label_set_text(s_ota_state, "");
    lv_obj_set_style_text_font(s_ota_state, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(s_ota_state, LV_ALIGN_TOP_MID, 0, 58);

    s_ota_detail = lv_label_create(card);
    lv_label_set_text(s_ota_detail, "");
    lv_obj_set_style_text_color(s_ota_detail, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ota_detail, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ota_detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(s_ota_detail, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_ota_detail, LV_PCT(100));
    lv_obj_align(s_ota_detail, LV_ALIGN_TOP_MID, 0, 84);

    s_ota_bar = lv_bar_create(card);
    lv_obj_set_size(s_ota_bar, LV_PCT(100), 8);
    lv_obj_align(s_ota_bar, LV_ALIGN_TOP_MID, 0, 128);
    lv_bar_set_range(s_ota_bar, 0, 100);
    lv_obj_set_style_bg_color(s_ota_bar, lv_color_hex(0x1E2A3A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ota_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_flag(s_ota_bar, LV_OBJ_FLAG_HIDDEN);

    s_ota_pct = lv_label_create(card);
    lv_label_set_text(s_ota_pct, "0%");
    lv_obj_set_style_text_color(s_ota_pct, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ota_pct, ui_font(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_align(s_ota_pct, LV_ALIGN_TOP_MID, 0, 142);
    lv_obj_add_flag(s_ota_pct, LV_OBJ_FLAG_HIDDEN);

    s_ota_primary = lv_button_create(card);
    ui_style_button(s_ota_primary, UI_COLOR_ACCENT);
    lv_obj_set_size(s_ota_primary, LV_PCT(100), 38);
    lv_obj_align(s_ota_primary, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_ota_primary, on_ota_primary, LV_EVENT_CLICKED, NULL);

    s_ota_primary_label = lv_label_create(s_ota_primary);
    lv_label_set_text(s_ota_primary_label, "");
    lv_obj_set_style_text_font(s_ota_primary_label, ui_font_text(UI_FONT_BODY),
                               LV_PART_MAIN);
    lv_obj_center(s_ota_primary_label);

    s_ota_secondary = ui_button(body, i18n(STR_LATER), lv_color_hex(0x1B2636),
                                on_ota_later, NULL);

    if (strlen(CONFIG_WATCH_OTA_MANIFEST_URL) == 0) {
        lv_obj_t *note = lv_label_create(body);
        lv_label_set_text(note,
            "No update server configured. Set WATCH_OTA_MANIFEST_URL in menuconfig.");
        lv_obj_set_style_text_color(note, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
        lv_obj_set_style_text_font(note, ui_font(UI_FONT_TINY), LV_PART_MAIN);
        lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(note, LV_PCT(100));
    }

    ota_refresh();
    ui_screen_subscribe(scr, on_ota_event);
    return scr;
}

/* ========================================================= 20 power off == */

static lv_obj_t *s_slider;

/* A slide-to-confirm rather than a button: powering the watch off by
 * accident in a pocket would be a bad day. */
static void on_slide(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    const int32_t v = lv_slider_get_value(slider);

    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        if (v >= 95) {
            svc_power_shutdown();
        } else {
            /* Not far enough: spring back. */
            lv_slider_set_value(slider, 0, LV_ANIM_ON);
        }
    }
}

static void on_restart(lv_event_t *e)
{
    (void)e;
    svc_power_restart();
}

lv_obj_t *scr_poweroff_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_POWER_OFF, &body);

    lv_obj_t *card = ui_card(body, 260);

    lv_obj_t *ring = lv_obj_create(card);
    ui_style_plain(ring);
    lv_obj_set_size(ring, 96, 96);
    lv_obj_set_style_radius(ring, 48, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ring, UI_COLOR_RED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ring, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(ring, UI_COLOR_RED, LV_PART_MAIN);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *ico = lv_label_create(ring);
    lv_label_set_text(ico, LV_SYMBOL_POWER);
    lv_obj_set_style_text_color(ico, UI_COLOR_RED, LV_PART_MAIN);
    lv_obj_set_style_text_font(ico, ui_font(UI_FONT_LARGE), LV_PART_MAIN);
    lv_obj_center(ico);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, i18n(STR_SLIDE_TO_POWER_OFF));
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 118);

    s_slider = lv_slider_create(card);
    lv_obj_set_size(s_slider, LV_PCT(100), 52);
    lv_obj_align(s_slider, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_slider_set_range(s_slider, 0, 100);
    lv_slider_set_value(s_slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_slider, 26, LV_PART_MAIN);
    lv_obj_set_style_radius(s_slider, 26, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_slider, 24, LV_PART_KNOB);
    lv_obj_set_style_bg_color(s_slider, lv_color_hex(0x2A1116), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider, UI_COLOR_RED, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider, UI_COLOR_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider, 2, LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider, on_slide, LV_EVENT_RELEASED, NULL);

    lv_obj_t *arrow = lv_label_create(s_slider);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_align(arrow, LV_ALIGN_LEFT_MID, 16, 0);

    ui_button(body, i18n(STR_RESTART), lv_color_hex(0x1B2636), on_restart, NULL);

    return scr;
}
