/*
 * 03 - Home dashboard.
 *
 * The design set shows a heart-rate tile here. This board has no optical
 * sensor, so rather than print a plausible-looking number that is not
 * measured, the tile carries the step count from the IMU's pedometer and
 * the layout is otherwise as drawn.
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
#include "bsp/esp-bsp.h"

static lv_obj_t *s_clock;
static lv_obj_t *s_date;
static lv_obj_t *s_steps_val;
static lv_obj_t *s_steps_bar;
static lv_obj_t *s_temp_val;
static lv_obj_t *s_batt_val;
static lv_obj_t *s_batt_ring;

static void go_sensor(lv_event_t *e)   { (void)e; ui_nav_go(UI_SCR_SENSOR); }
static void go_battery(lv_event_t *e)  { (void)e; ui_nav_go(UI_SCR_BATTERY); }
static void go_apps(lv_event_t *e)     { (void)e; ui_nav_go(UI_SCR_APPS); }

static void refresh(void)
{
    char buf[28];
    svc_time_format_clock(buf, sizeof(buf));
    lv_label_set_text(s_clock, buf);
    svc_time_format_date(buf, sizeof(buf));
    lv_label_set_text(s_date, buf);

    const uint32_t steps = svc_sensors_steps();
    const uint32_t goal = svc_settings_get()->step_goal;
    lv_label_set_text_fmt(s_steps_val, "%lu", (unsigned long)steps);
    const int32_t pct = (goal > 0) ? (int32_t)((uint64_t)steps * 100 / goal) : 0;
    lv_bar_set_value(s_steps_bar, (pct > 100) ? 100 : pct, LV_ANIM_ON);

    if (svc_sensors_available()) {
        lv_label_set_text_fmt(s_temp_val, "%.0f\xC2\xB0", (double)svc_sensors_latest()->temp_c);
    } else {
        lv_label_set_text(s_temp_val, "--");
    }

    const axp2101_status_t *pw = svc_power_status();
    if (pw->percent <= 100) {
        lv_label_set_text_fmt(s_batt_val, "%u%%", (unsigned)pw->percent);
        lv_arc_set_value(s_batt_ring, pw->percent);
        lv_obj_set_style_arc_color(s_batt_ring,
                                   pw->charging ? UI_COLOR_GREEN
                                                : (pw->percent <= 15 ? UI_COLOR_RED
                                                                     : UI_COLOR_CYAN),
                                   LV_PART_INDICATOR);
    } else {
        lv_label_set_text(s_batt_val, "--");
        lv_arc_set_value(s_batt_ring, 0);
    }
}

static void on_tick(lv_timer_t *t)
{
    (void)t;
    refresh();
}

lv_obj_t *scr_home_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_TODAY, &body);

    /* ---- clock card ---- */
    lv_obj_t *head = ui_card(body, 92);
    lv_obj_add_flag(head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(head, go_apps, LV_EVENT_CLICKED, NULL);

    s_clock = lv_label_create(head);
    lv_label_set_text(s_clock, "00:00");
    lv_obj_set_style_text_color(s_clock, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_clock, ui_font(UI_FONT_LARGE), LV_PART_MAIN);
    lv_obj_align(s_clock, LV_ALIGN_LEFT_MID, 0, -6);

    s_date = lv_label_create(head);
    lv_label_set_text(s_date, "");
    lv_obj_set_style_text_color(s_date, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_date, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(s_date, LV_ALIGN_LEFT_MID, 2, 26);

    s_batt_ring = ui_ring(head, 62, UI_COLOR_CYAN, 0, "--", &s_batt_val);
    lv_obj_align(s_batt_ring, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_font(s_batt_val, ui_font(UI_FONT_SMALL), LV_PART_MAIN);

    /* ---- activity card ---- */
    lv_obj_t *act = ui_card(body, 96);
    ui_style_card_pressable(act);
    lv_obj_add_event_cb(act, go_sensor, LV_EVENT_CLICKED, NULL);

    lv_obj_t *act_cap = lv_label_create(act);
    lv_label_set_text(act_cap, i18n(STR_STEPS));
    lv_obj_set_style_text_color(act_cap, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(act_cap, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(act_cap, LV_ALIGN_TOP_LEFT, 0, 0);

    s_steps_val = lv_label_create(act);
    lv_label_set_text(s_steps_val, "0");
    lv_obj_set_style_text_color(s_steps_val, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_steps_val, ui_font(UI_FONT_LARGE), LV_PART_MAIN);
    lv_obj_align(s_steps_val, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *goal = lv_label_create(act);
    lv_label_set_text_fmt(goal, "%s %lu", i18n(STR_GOAL),
                          (unsigned long)svc_settings_get()->step_goal);
    lv_obj_set_style_text_color(goal, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(goal, ui_font_text(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_align(goal, LV_ALIGN_TOP_RIGHT, 0, 2);

    s_steps_bar = lv_bar_create(act);
    lv_obj_set_size(s_steps_bar, LV_PCT(100), 6);
    lv_obj_align(s_steps_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(s_steps_bar, 0, 100);
    lv_bar_set_value(s_steps_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_steps_bar, lv_color_hex(0x1E2A3A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_steps_bar, UI_COLOR_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_steps_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_steps_bar, 3, LV_PART_INDICATOR);

    /* ---- two small tiles ---- */
    lv_obj_t *row = lv_obj_create(body);
    ui_style_plain(row);
    lv_obj_set_size(row, LV_PCT(100), 88);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *t1 = ui_stat_tile(row, LV_SYMBOL_SETTINGS, UI_COLOR_ORANGE,
                                i18n(STR_TEMPERATURE), "--", &s_temp_val);
    lv_obj_set_size(t1, (UI_CONTENT_W - UI_GAP) / 2, 84);
    ui_style_card_pressable(t1);
    lv_obj_add_event_cb(t1, go_sensor, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t2 = ui_stat_tile(row, LV_SYMBOL_CHARGE, UI_COLOR_GREEN,
                                i18n(STR_BATTERY), "--", NULL);
    lv_obj_set_size(t2, (UI_CONTENT_W - UI_GAP) / 2, 84);
    ui_style_card_pressable(t2);
    lv_obj_add_event_cb(t2, go_battery, LV_EVENT_CLICKED, NULL);
    /* Reuse the ring's label for the percentage; this tile shows voltage. */
    lv_obj_t *volt = lv_obj_get_child(t2, 1);
    if (volt != NULL) {
        const axp2101_status_t *pw = svc_power_status();
        lv_label_set_text_fmt(volt, "%.2fV", (double)pw->vbat_mv / 1000.0);
    }

    /* ---- shortcut ---- */
    ui_menu_row(body, LV_SYMBOL_LIST, UI_COLOR_ACCENT, i18n(STR_APPS), go_apps, NULL);

    refresh();
    ui_screen_add_timer(scr, on_tick, 1000);
    return scr;
}
