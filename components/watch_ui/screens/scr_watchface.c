/*
 * 02 - Watch face.
 *
 * The screen the watch sits on. Tapping anywhere opens the dashboard;
 * swiping up opens the app grid.
 *
 * Deliberately cheap to redraw: one label changes per minute, and the
 * second-by-second timer only rewrites text when the string actually
 * differs, so an idle watch face costs almost nothing.
 */
#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_power.h"
#include "watch_svc/svc_sensors.h"
#include "watch_svc/svc_settings.h"
#include "watch_svc/svc_time.h"

#include <stdio.h>
#include <string.h>
#include "bsp/esp-bsp.h"

static lv_obj_t *s_clock;
static lv_obj_t *s_ampm;
static lv_obj_t *s_date;
static lv_obj_t *s_batt_label;
static lv_obj_t *s_batt_icon;
static lv_obj_t *s_steps;
static lv_obj_t *s_temp;
static lv_obj_t *s_batt_mini;

static void refresh_clock(void)
{
    char buf[12];
    svc_time_format_clock(buf, sizeof(buf));
    if (strcmp(lv_label_get_text(s_clock), buf) != 0) {
        lv_label_set_text(s_clock, buf);
    }

    char ampm[6];
    svc_time_format_ampm(ampm, sizeof(ampm));
    lv_label_set_text(s_ampm, ampm);

    char date[28];
    svc_time_format_date(date, sizeof(date));
    if (strcmp(lv_label_get_text(s_date), date) != 0) {
        lv_label_set_text(s_date, date);
    }
}

static void refresh_stats(void)
{
    const axp2101_status_t *pw = svc_power_status();
    if (pw->percent <= 100) {
        lv_label_set_text_fmt(s_batt_label, "%u%%", (unsigned)pw->percent);
        lv_label_set_text_fmt(s_batt_mini, "%u", (unsigned)pw->percent);
        lv_obj_set_style_text_color(s_batt_icon,
                                    pw->charging ? UI_COLOR_GREEN
                                                 : (pw->percent <= 15 ? UI_COLOR_RED
                                                                      : UI_COLOR_TEXT_DIM),
                                    LV_PART_MAIN);
        lv_label_set_text(s_batt_icon, pw->charging ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_FULL);
    } else {
        lv_label_set_text(s_batt_label, "--");
        lv_label_set_text(s_batt_mini, "--");
    }

    lv_label_set_text_fmt(s_steps, "%lu", (unsigned long)svc_sensors_steps());

    const svc_sensors_sample_t *s = svc_sensors_latest();
    if (svc_sensors_available()) {
        lv_label_set_text_fmt(s_temp, "%.0f", (double)s->temp_c);
    } else {
        lv_label_set_text(s_temp, "--");
    }
}

static void on_tick(lv_timer_t *t)
{
    (void)t;
    refresh_clock();
    refresh_stats();
}

static void on_clicked(lv_event_t *e)
{
    (void)e;
    svc_power_notify_activity();
    ui_nav_go(UI_SCR_HOME);
}

static void on_gesture(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    svc_power_notify_activity();
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
        lv_indev_wait_release(indev);
        ui_nav_go(UI_SCR_APPS);
    }
}

/* One tile in the strip along the bottom. */
static lv_obj_t *mini_stat(lv_obj_t *parent, const char *icon, lv_color_t color,
                           const char *unit, lv_obj_t **out_value)
{
    lv_obj_t *box = lv_obj_create(parent);
    ui_style_plain(box);
    lv_obj_set_size(box, 118, 62);

    lv_obj_t *ico = lv_label_create(box);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_color(ico, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(ico, ui_font(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *val = lv_label_create(box);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(val, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
    lv_obj_align(val, LV_ALIGN_CENTER, 0, 4);

    lv_obj_t *u = lv_label_create(box);
    lv_label_set_text(u, unit);
    lv_obj_set_style_text_color(u, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(u, ui_font_text(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_align(u, LV_ALIGN_BOTTOM_MID, 0, 0);

    if (out_value != NULL) {
        *out_value = val;
    }
    return box;
}

lv_obj_t *scr_watchface_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_style_plain(scr);
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, NULL);

    /* ---- status strip ---- */
    lv_obj_t *strip = lv_obj_create(scr);
    ui_style_plain(strip);
    lv_obj_set_size(strip, UI_CONTENT_W, 24);
    lv_obj_align(strip, LV_ALIGN_TOP_MID, 0, 14);

    s_batt_icon = lv_label_create(strip);
    lv_label_set_text(s_batt_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(s_batt_icon, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_batt_icon, ui_font(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(s_batt_icon, LV_ALIGN_RIGHT_MID, -36, 0);

    s_batt_label = lv_label_create(strip);
    lv_label_set_text(s_batt_label, "--");
    lv_obj_set_style_text_color(s_batt_label, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_batt_label, ui_font(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(s_batt_label, LV_ALIGN_RIGHT_MID, 0, 0);

    s_date = lv_label_create(strip);
    lv_label_set_text(s_date, "");
    lv_obj_set_style_text_color(s_date, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_date, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(s_date, LV_ALIGN_LEFT_MID, 0, 0);

    /* ---- the time ---- */
    s_clock = lv_label_create(scr);
    lv_label_set_text(s_clock, "00:00");
    lv_obj_set_style_text_color(s_clock, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_clock, ui_font(UI_FONT_HUGE), LV_PART_MAIN);
    lv_obj_align(s_clock, LV_ALIGN_CENTER, 0, -70);

    s_ampm = lv_label_create(scr);
    lv_label_set_text(s_ampm, "");
    lv_obj_set_style_text_color(s_ampm, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ampm, ui_font(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align_to(s_ampm, s_clock, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -8);

    /* A thin accent rule under the clock, as in the design. */
    lv_obj_t *rule = lv_obj_create(scr);
    ui_style_plain(rule);
    lv_obj_set_size(rule, 120, 3);
    lv_obj_set_style_radius(rule, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rule, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(rule, LV_ALIGN_CENTER, 0, -18);

    /* ---- stats strip ---- */
    lv_obj_t *stats = lv_obj_create(scr);
    ui_style_plain(stats);
    lv_obj_set_size(stats, UI_CONTENT_W, 70);
    lv_obj_align(stats, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    mini_stat(stats, LV_SYMBOL_GPS, UI_COLOR_GREEN, i18n(STR_STEPS), &s_steps);
    mini_stat(stats, LV_SYMBOL_SETTINGS, UI_COLOR_ORANGE, "\xC2\xB0""C", &s_temp);
    mini_stat(stats, LV_SYMBOL_BATTERY_3, UI_COLOR_CYAN, i18n(STR_BATTERY), &s_batt_mini);

    /* ---- hint ---- */
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, ui_font(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);

    refresh_clock();
    refresh_stats();
    ui_screen_add_timer(scr, on_tick, 1000);
    return scr;
}
