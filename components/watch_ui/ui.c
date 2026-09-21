#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_power.h"
#include "watch_svc/svc_settings.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#include "bsp/esp-bsp.h"

static const char *TAG = "ui";

#define TOAST_MS 3000

static lv_obj_t *s_toast;

/* ------------------------------------------------------------------- toast */

static void toast_closed(lv_timer_t *t)
{
    (void)t;   /* repeat_count is 1, so LVGL deletes the timer itself */
    if (s_toast != NULL) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
}

void ui_toast(const char *text, lv_color_t color)
{
    if (text == NULL) {
        return;
    }
    if (!bsp_display_lock(200)) {
        return;
    }

    if (s_toast != NULL) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }

    /* Parented to the layer above the screen so it survives navigation and
     * is never clipped by a scrolling body. */
    lv_obj_t *layer = lv_layer_top();
    s_toast = lv_obj_create(layer);
    ui_style_plain(s_toast);
    lv_obj_set_size(s_toast, UI_SCREEN_W - 2 * UI_PAD, 40);
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, UI_HEADER_H + 6);
    lv_obj_set_style_radius(s_toast, UI_RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_toast, UI_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_toast, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_toast, color, LV_PART_MAIN);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(s_toast);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(lbl, UI_SCREEN_W - 2 * UI_PAD - 24);
    lv_obj_center(lbl);

    lv_timer_t *t = lv_timer_create(toast_closed, TOAST_MS, NULL);
    lv_timer_set_repeat_count(t, 1);

    bsp_display_unlock();
}

/* ---------------------------------------------------------- global events */

static void on_watch_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;

    switch (id) {
    case WATCH_EV_LANG_CHANGED: {
        const watch_lang_t lang = *(const watch_lang_t *)data;
        if (bsp_display_lock(1000)) {
            ui_theme_set_lang(lang);
            /* Every label was built with the old strings, so the only
             * honest way to switch language is to rebuild the screen. */
            ui_nav_rebuild();
            bsp_display_unlock();
        }
        break;
    }

    case WATCH_EV_LOW_BATTERY: {
        const uint8_t pct = *(const uint8_t *)data;
        char msg[48];
        snprintf(msg, sizeof(msg), "%s %u%%", i18n(STR_BATTERY), (unsigned)pct);
        ui_toast(msg, UI_COLOR_RED);
        break;
    }

    case WATCH_EV_DISPLAY_WAKE:
        /* Coming back from sleep lands on the watch face, the way a watch
         * should behave - not on whatever screen was open an hour ago. */
        if (bsp_display_lock(500)) {
            if (!ui_nav_is_current(UI_SCR_WATCHFACE) && !ui_nav_is_current(UI_SCR_HOME)) {
                ui_nav_replace(UI_SCR_WATCHFACE);
            }
            bsp_display_unlock();
        }
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------ activity */

/*
 * Any press anywhere resets the idle countdown.
 *
 * Hooking the input device rather than each screen means a drag on a
 * slider, a scroll, or a tap on dead space all count as activity - which is
 * what a user expects. Without this the screen would dim mid-gesture on any
 * screen whose widgets happen not to emit a click.
 */
static void on_any_press(lv_event_t *e)
{
    (void)e;
    svc_power_notify_activity();
}

/* -------------------------------------------------------------- boot flow */

static void leave_splash(lv_timer_t *t)
{
    (void)t;   /* one-shot: LVGL frees it after this call */
    ui_nav_replace(UI_SCR_WATCHFACE);
}

esp_err_t ui_start(void)
{
    if (!bsp_display_lock(2000)) {
        ESP_LOGE(TAG, "cannot take the LVGL lock");
        return ESP_ERR_TIMEOUT;
    }

    ui_theme_set_lang(svc_settings_get()->lang);
    ui_theme_init();
    ui_nav_init(UI_SCR_BOOT);

    lv_indev_t *touch = bsp_display_get_input_dev();
    if (touch != NULL) {
        lv_indev_add_event_cb(touch, on_any_press, LV_EVENT_PRESSED, NULL);
    }

    /* Hold the splash briefly so it reads as a boot screen rather than a
     * flash of colour. */
    lv_timer_t *t = lv_timer_create(leave_splash, 1600, NULL);
    lv_timer_set_repeat_count(t, 1);

    bsp_display_unlock();

    svc_event_subscribe(ESP_EVENT_ANY_ID, on_watch_event, NULL);
    ESP_LOGI(TAG, "UI started");
    return ESP_OK;
}
