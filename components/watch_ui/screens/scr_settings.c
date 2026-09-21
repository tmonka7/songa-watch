/*
 * 15 - Settings, 16 - Display, 26 - Time, 27 - Language.
 *
 * The settings tree. Every change goes through svc_settings, which persists
 * it and announces it, so nothing here has to remember state across a
 * rebuild - the screen reads the live struct when it is built.
 */
#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_ble.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_power.h"
#include "watch_svc/svc_settings.h"
#include "watch_svc/svc_time.h"
#include "watch_svc/svc_wifi.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "bsp/esp-bsp.h"

/* ========================================================== 15 settings == */

static void on_row(lv_event_t *e)
{
    const ui_screen_id_t target = (ui_screen_id_t)(uintptr_t)lv_event_get_user_data(e);
    ui_nav_go(target);
}

lv_obj_t *scr_settings_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_SETTINGS, &body);

    struct {
        const char    *icon;
        lv_color_t     color;
        i18n_id_t      label;
        ui_screen_id_t target;
    } rows[] = {
        { LV_SYMBOL_IMAGE,     UI_COLOR_ORANGE, STR_DISPLAY,    UI_SCR_DISPLAY    },
        { LV_SYMBOL_WIFI,      UI_COLOR_CYAN,   STR_WIFI,       UI_SCR_WIFI       },
        { LV_SYMBOL_BLUETOOTH, UI_COLOR_ACCENT, STR_BLUETOOTH,  UI_SCR_BLUETOOTH  },
        { LV_SYMBOL_GPS,       UI_COLOR_GREEN,  STR_SENSORS,    UI_SCR_SENSOR     },
        { LV_SYMBOL_AUDIO,     UI_COLOR_PINK,   STR_AUDIO,      UI_SCR_AUDIO      },
        { LV_SYMBOL_SD_CARD,   UI_COLOR_PURPLE, STR_STORAGE,    UI_SCR_SDCARD     },
        { LV_SYMBOL_BELL,      UI_COLOR_ACCENT, STR_TIME,       UI_SCR_TIME       },
        { LV_SYMBOL_LIST,      UI_COLOR_CYAN,   STR_LANGUAGE,   UI_SCR_LANGUAGE   },
        { LV_SYMBOL_DRIVE,     UI_COLOR_RED,    STR_OBD2,       UI_SCR_OBD2_HOME  },
        { LV_SYMBOL_DOWNLOAD,  UI_COLOR_GREEN,  STR_OTA_UPDATE, UI_SCR_OTA        },
        { LV_SYMBOL_EYE_OPEN,  UI_COLOR_TEXT_FAINT, STR_ABOUT,  UI_SCR_ABOUT      },
        { LV_SYMBOL_POWER,     UI_COLOR_RED,    STR_POWER_OFF,  UI_SCR_POWEROFF   },
    };

    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        ui_menu_row(body, rows[i].icon, rows[i].color, i18n(rows[i].label),
                    on_row, (void *)(uintptr_t)rows[i].target);
    }
    return scr;
}

/* =========================================================== 16 display == */

static lv_obj_t *s_bright_label;

static void on_brightness(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    const int32_t v = lv_slider_get_value(slider);
    svc_power_set_brightness((uint8_t)v);
    lv_label_set_text_fmt(s_bright_label, "%d%%", (int)v);
}

static void on_always_on(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    svc_settings_set_always_on(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void on_timeout(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target_obj(e);
    /* Must match the option list built below. */
    static const uint16_t k_seconds[] = { 10, 15, 30, 60, 120, 300 };
    const uint32_t sel = lv_dropdown_get_selected(dd);
    if (sel < sizeof(k_seconds) / sizeof(k_seconds[0])) {
        svc_settings_set_idle_off_sec(k_seconds[sel]);
        /* Keep dimming comfortably ahead of the blank. */
        const uint16_t dim = (uint16_t)(k_seconds[sel] / 3);
        svc_settings_set_idle_dim_sec((dim < 3) ? 3 : dim);
    }
}

static void on_watchface(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target_obj(e);
    svc_settings_set_watchface((uint8_t)lv_dropdown_get_selected(dd));
}

static lv_obj_t *dropdown_row(lv_obj_t *parent, const char *label, const char *options,
                              uint32_t selected, lv_event_cb_t cb)
{
    lv_obj_t *card = ui_card(parent, 54);
    lv_obj_set_style_pad_hor(card, 12, LV_PART_MAIN);

    lv_obj_t *l = lv_label_create(card);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(l, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *dd = lv_dropdown_create(card);
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, selected);
    lv_obj_set_width(dd, 140);
    lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(dd, lv_color_hex(0x1B2636), LV_PART_MAIN);
    lv_obj_set_style_border_color(dd, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_text_color(dd, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(dd, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list != NULL) {
        lv_obj_set_style_bg_color(list, UI_COLOR_CARD, LV_PART_MAIN);
        lv_obj_set_style_text_color(list, UI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(list, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    }
    return dd;
}

lv_obj_t *scr_display_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_DISPLAY, &body);
    const watch_settings_t *cfg = svc_settings_get();

    /* ---- brightness ---- */
    lv_obj_t *card = ui_card(body, 76);

    lv_obj_t *l = lv_label_create(card);
    lv_label_set_text(l, i18n(STR_BRIGHTNESS));
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(l, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);

    s_bright_label = lv_label_create(card);
    lv_label_set_text_fmt(s_bright_label, "%u%%", (unsigned)cfg->brightness);
    lv_obj_set_style_text_color(s_bright_label, UI_COLOR_ORANGE, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_bright_label, ui_font(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(s_bright_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_size(slider, LV_PCT(100), 10);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -2);
    /* 1 is the floor, not 0: a zero-brightness AMOLED looks like a crash. */
    lv_slider_set_range(slider, 1, 100);
    lv_slider_set_value(slider, cfg->brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x1E2A3A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, UI_COLOR_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, UI_COLOR_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, on_brightness, LV_EVENT_VALUE_CHANGED, NULL);

    /* ---- timeout ---- */
    uint32_t sel = 2;
    switch (cfg->idle_off_sec) {
    case 10:  sel = 0; break;
    case 15:  sel = 1; break;
    case 30:  sel = 2; break;
    case 60:  sel = 3; break;
    case 120: sel = 4; break;
    case 300: sel = 5; break;
    default:  sel = 2; break;
    }
    dropdown_row(body, i18n(STR_AUTO_TIMEOUT),
                 "10 sec\n15 sec\n30 sec\n1 min\n2 min\n5 min", sel, on_timeout);

    /* ---- always on ---- */
    ui_toggle_row(body, i18n(STR_ALWAYS_ON), cfg->always_on, on_always_on, NULL);

    /* ---- watch face ---- */
    dropdown_row(body, i18n(STR_WATCH_FACE), "Digital\nMinimal\nBold",
                 cfg->watchface, on_watchface);

    lv_obj_t *note = lv_label_create(body);
    lv_label_set_text(note, "Always On keeps a dim clock lit. It costs battery.");
    lv_obj_set_style_text_color(note, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(note, ui_font(UI_FONT_TINY), LV_PART_MAIN);
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(note, LV_PCT(100));

    return scr;
}

/* ============================================================== 26 time == */

static lv_obj_t *s_roll_hour;
static lv_obj_t *s_roll_min;
static lv_obj_t *s_now_label;

static void on_24h(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    svc_settings_set_time_24h(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void on_ntp(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    svc_settings_set_ntp_enable(on);
    if (on) {
        if (svc_wifi_status()->state == SVC_WIFI_CONNECTED) {
            svc_time_sync_now();
            ui_toast(i18n(STR_SYNC_NETWORK), UI_COLOR_ACCENT);
        } else {
            ui_toast(i18n(STR_NEEDS_WIFI), UI_COLOR_ORANGE);
        }
    }
}

static void on_tz(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target_obj(e);
    const uint32_t sel = lv_dropdown_get_selected(dd);
    svc_time_set_timezone(svc_time_tz_posix(sel));
}

static void on_apply_time(lv_event_t *e)
{
    (void)e;
    struct tm now;
    svc_time_now(&now);
    now.tm_hour = (int)lv_roller_get_selected(s_roll_hour);
    now.tm_min  = (int)lv_roller_get_selected(s_roll_min);
    now.tm_sec  = 0;

    if (svc_time_set_local(&now) == ESP_OK) {
        ui_toast(i18n(STR_SAVE), UI_COLOR_GREEN);
    } else {
        ui_toast(i18n(STR_ERROR), UI_COLOR_RED);
    }
}

static void time_tick(lv_timer_t *t)
{
    (void)t;
    char clock[12], date[28];
    svc_time_format_clock(clock, sizeof(clock));
    svc_time_format_date(date, sizeof(date));
    lv_label_set_text_fmt(s_now_label, "%s  %s", clock, date);
}

static lv_obj_t *make_roller(lv_obj_t *parent, const char *options, uint32_t selected)
{
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, options, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(r, 3);
    lv_roller_set_selected(r, selected, LV_ANIM_OFF);
    lv_obj_set_width(r, 90);
    lv_obj_set_style_bg_color(r, lv_color_hex(0x0E1520), LV_PART_MAIN);
    lv_obj_set_style_border_color(r, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_text_color(r, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(r, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(r, UI_COLOR_ACCENT, LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, UI_COLOR_TEXT, LV_PART_SELECTED);
    return r;
}

lv_obj_t *scr_time_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_TIME_SETTINGS, &body);
    const watch_settings_t *cfg = svc_settings_get();

    /* ---- current time ---- */
    lv_obj_t *head = ui_card(body, 56);
    s_now_label = lv_label_create(head);
    lv_label_set_text(s_now_label, "");
    lv_obj_set_style_text_color(s_now_label, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_now_label, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_center(s_now_label);

    /* ---- manual set ---- */
    lv_obj_t *card = ui_card(body, 150);

    lv_obj_t *cap = lv_label_create(card);
    lv_label_set_text(cap, i18n(STR_SET_MANUALLY));
    lv_obj_set_style_text_color(cap, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(cap, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Built once as static strings; lv_roller keeps its own copy anyway. */
    static char hours[24 * 3 + 1];
    static char mins[60 * 3 + 1];
    if (hours[0] == '\0') {
        char *p = hours;
        for (int h = 0; h < 24; h++) {
            p += sprintf(p, "%02d%s", h, (h < 23) ? "\n" : "");
        }
        p = mins;
        for (int m = 0; m < 60; m++) {
            p += sprintf(p, "%02d%s", m, (m < 59) ? "\n" : "");
        }
    }

    struct tm now;
    svc_time_now(&now);

    s_roll_hour = make_roller(card, hours, (uint32_t)now.tm_hour);
    lv_obj_align(s_roll_hour, LV_ALIGN_CENTER, -60, 8);

    lv_obj_t *colon = lv_label_create(card);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_color(colon, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(colon, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
    lv_obj_align(colon, LV_ALIGN_CENTER, -8, 8);

    s_roll_min = make_roller(card, mins, (uint32_t)now.tm_min);
    lv_obj_align(s_roll_min, LV_ALIGN_CENTER, 44, 8);

    lv_obj_t *apply = ui_button(card, i18n(STR_SAVE), UI_COLOR_ACCENT,
                                on_apply_time, NULL);
    lv_obj_set_size(apply, 90, 36);
    lv_obj_align(apply, LV_ALIGN_RIGHT_MID, 0, 8);

    /* ---- options ---- */
    ui_toggle_row(body, i18n(STR_24_HOUR), cfg->time_24h, on_24h, NULL);
    ui_toggle_row(body, i18n(STR_SYNC_NETWORK), cfg->ntp_enable, on_ntp, NULL);

    /* Timezone list, built from the service's table. */
    static char tz_options[512];
    tz_options[0] = '\0';
    uint32_t tz_sel = 0;
    {
        char *p = tz_options;
        const size_t count = svc_time_tz_count();
        for (size_t i = 0; i < count; i++) {
            const char *label = svc_time_tz_label(i);
            const size_t remaining = sizeof(tz_options) - (size_t)(p - tz_options);
            const int n = snprintf(p, remaining, "%s%s", label,
                                   (i + 1 < count) ? "\n" : "");
            if (n < 0 || (size_t)n >= remaining) {
                break;
            }
            p += n;
            if (strcmp(svc_time_tz_posix(i), cfg->tz) == 0) {
                tz_sel = (uint32_t)i;
            }
        }
    }
    dropdown_row(body, i18n(STR_TIME_ZONE), tz_options, tz_sel, on_tz);

    time_tick(NULL);
    ui_screen_add_timer(scr, time_tick, 1000);
    return scr;
}

/* ========================================================== 27 language == */

static void on_language(lv_event_t *e)
{
    const watch_lang_t lang = (watch_lang_t)(uintptr_t)lv_event_get_user_data(e);
    if (lang == svc_settings_get()->lang) {
        return;
    }
    svc_settings_set_lang(lang);
    /* i18n_set_lang posts WATCH_EV_LANG_CHANGED, which rebuilds the screen
     * with the new strings and the right font. */
    i18n_set_lang(lang);
}

lv_obj_t *scr_language_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_LANGUAGE, &body);
    const watch_lang_t current = svc_settings_get()->lang;

    const struct {
        i18n_id_t    label;
        const char  *native;
        watch_lang_t lang;
    } langs[] = {
        { STR_ENGLISH,  "English", WATCH_LANG_EN },
        { STR_JAPANESE, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", WATCH_LANG_JP },
    };

    for (size_t i = 0; i < sizeof(langs) / sizeof(langs[0]); i++) {
        lv_obj_t *row = ui_card(body, 56);
        ui_style_card_pressable(row);
        lv_obj_set_style_pad_hor(row, 14, LV_PART_MAIN);
        lv_obj_add_event_cb(row, on_language, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)langs[i].lang);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, langs[i].native);
        lv_obj_set_style_text_color(name, UI_COLOR_TEXT, LV_PART_MAIN);
        /* Always the CJK face here: the Japanese option has to be legible
         * even while the interface is still in English. */
        lv_obj_set_style_text_font(name, &lv_font_source_han_sans_sc_16_cjk,
                                   LV_PART_MAIN);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

        if (langs[i].lang == current) {
            lv_obj_t *tick = lv_label_create(row);
            lv_label_set_text(tick, LV_SYMBOL_OK);
            lv_obj_set_style_text_color(tick, UI_COLOR_GREEN, LV_PART_MAIN);
            lv_obj_align(tick, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }

    lv_obj_t *note = lv_label_create(body);
    lv_label_set_text(note,
        "Japanese uses the bundled Source Han Sans subset. See docs/i18n.md.");
    lv_obj_set_style_text_color(note, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(note, ui_font(UI_FONT_TINY), LV_PART_MAIN);
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(note, LV_PCT(100));

    return scr;
}
