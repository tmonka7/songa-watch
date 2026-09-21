/*
 * 07 - Battery.
 *
 * Everything the AXP2101 will tell us. The design shows a current reading;
 * this PMU has no battery-current ADC, so the row reports the direction of
 * flow (charging or discharging) instead of inventing a milliamp figure.
 */
#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_power.h"
#include "watch_svc/svc_settings.h"

#include <stdio.h>
#include "bsp/esp-bsp.h"

static lv_obj_t *s_ring;
static lv_obj_t *s_ring_label;
static lv_obj_t *s_vbat;
static lv_obj_t *s_vsys;
static lv_obj_t *s_flow;
static lv_obj_t *s_state;
static lv_obj_t *s_temp;
static lv_obj_t *s_vbus;
static lv_obj_t *s_saving_sw;

static const char *charge_state_text(axp2101_chg_state_t st, bool charging)
{
    switch (st) {
    case AXP2101_CHG_TRICKLE: return "Trickle";
    case AXP2101_CHG_PRE:     return "Pre-charge";
    case AXP2101_CHG_CC:      return "Constant current";
    case AXP2101_CHG_CV:      return "Constant voltage";
    case AXP2101_CHG_DONE:    return "Done";
    case AXP2101_CHG_STOP:
    default:                  return charging ? "Charging" : "Idle";
    }
}

static void refresh(void)
{
    const axp2101_status_t *pw = svc_power_status();

    if (pw->battery_present && pw->percent <= 100) {
        lv_arc_set_value(s_ring, pw->percent);
        lv_label_set_text_fmt(s_ring_label, "%u%%", (unsigned)pw->percent);
        lv_obj_set_style_arc_color(s_ring,
                                   pw->charging ? UI_COLOR_GREEN
                                                : (pw->percent <= 15 ? UI_COLOR_RED
                                                                     : UI_COLOR_CYAN),
                                   LV_PART_INDICATOR);
    } else {
        lv_arc_set_value(s_ring, 0);
        lv_label_set_text(s_ring_label, "--");
    }

    lv_label_set_text_fmt(s_vbat, "%.2f V", (double)pw->vbat_mv / 1000.0);
    lv_label_set_text_fmt(s_vsys, "%.2f V", (double)pw->vsys_mv / 1000.0);

    if (!pw->battery_present) {
        lv_label_set_text(s_flow, i18n(STR_NO_BATTERY));
    } else {
        lv_label_set_text(s_flow, pw->charging ? i18n(STR_CHARGING) : i18n(STR_BATTERY));
    }

    lv_label_set_text(s_state, charge_state_text(pw->chg_state, pw->charging));
    lv_label_set_text_fmt(s_temp, "%.1f\xC2\xB0""C", (double)pw->die_temp_c);

    if (pw->vbus_present) {
        lv_label_set_text_fmt(s_vbus, "%.2f V", (double)pw->vbus_mv / 1000.0);
    } else {
        lv_label_set_text(s_vbus, i18n(STR_NONE));
    }
}

static void on_tick(lv_timer_t *t)
{
    (void)t;
    refresh();
}

/* Power saving here means a short screen timeout and a dim default - the
 * two settings that actually move the needle on runtime. */
static void on_saving(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (on) {
        svc_settings_set_idle_dim_sec(5);
        svc_settings_set_idle_off_sec(15);
        svc_power_set_brightness(40);
    } else {
        svc_settings_set_idle_dim_sec(CONFIG_WATCH_IDLE_DIM_SEC);
        svc_settings_set_idle_off_sec(CONFIG_WATCH_IDLE_SLEEP_SEC);
        svc_power_set_brightness(70);
    }
    ui_toast(on ? i18n(STR_POWER_SAVING) : i18n(STR_OFF), UI_COLOR_GREEN);
}

lv_obj_t *scr_battery_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_BATTERY, &body);

    if (!axp2101_is_present()) {
        ui_empty_state(body, LV_SYMBOL_WARNING, i18n(STR_NOT_AVAILABLE),
                       "AXP2101 did not answer");
        return scr;
    }

    /* ---- gauge ---- */
    lv_obj_t *top = ui_card(body, 160);
    s_ring = ui_ring(top, 130, UI_COLOR_CYAN, 0, "--", &s_ring_label);
    lv_obj_align(s_ring, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_font(s_ring_label, ui_font(UI_FONT_LARGE), LV_PART_MAIN);

    lv_obj_t *side = lv_obj_create(top);
    ui_style_plain(side);
    lv_obj_set_size(side, 190, 130);
    lv_obj_align(side, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(side, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    ui_kv_row(side, i18n(STR_VOLTAGE), "--", &s_vbat);
    ui_kv_row(side, i18n(STR_SYSTEM_VOLTAGE), "--", &s_vsys);
    ui_kv_row(side, i18n(STR_CURRENT), "--", &s_flow);

    /* ---- detail ---- */
    lv_obj_t *detail = ui_card(body, 0);
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
    ui_kv_row(detail, i18n(STR_CHARGE_STATE), "--", &s_state);
    ui_kv_row(detail, i18n(STR_USB_POWER), "--", &s_vbus);
    ui_kv_row(detail, i18n(STR_TEMPERATURE), "--", &s_temp);

    /* ---- power saving ---- */
    const watch_settings_t *cfg = svc_settings_get();
    const bool saving = (cfg->idle_off_sec <= 15);
    s_saving_sw = ui_toggle_row(body, i18n(STR_POWER_SAVING), saving, on_saving, NULL);

    refresh();
    ui_screen_add_timer(scr, on_tick, 1000);
    return scr;
}
