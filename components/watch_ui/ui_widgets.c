#include "watch_ui/ui_widgets.h"
#include "watch_ui/ui_nav.h"
#include "watch_svc/svc_power.h"
#include "watch_svc/svc_time.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ header */

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    svc_power_notify_activity();
    ui_nav_back();
}

lv_obj_t *ui_widgets_header(lv_obj_t *parent, const char *title)
{
    lv_obj_t *bar = lv_obj_create(parent);
    ui_style_plain(bar);
    lv_obj_set_size(bar, UI_SCREEN_W, UI_HEADER_H);
    lv_obj_set_style_bg_color(bar, UI_COLOR_HEADER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(bar, UI_PAD, LV_PART_MAIN);

    /* Back chevron. Generously sized for a fingertip even though the glyph
     * is small - the touch target matters more than the mark. */
    if (ui_nav_can_go_back()) {
        lv_obj_t *back = lv_button_create(bar);
        ui_style_plain(back);
        lv_obj_set_size(back, 40, UI_HEADER_H);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, -8, 0);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);

        lv_obj_t *chev = lv_label_create(back);
        lv_label_set_text(chev, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(chev, UI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(chev, ui_font(UI_FONT_BODY), LV_PART_MAIN);
        lv_obj_center(chev);
    }

    lv_obj_t *lbl = lv_label_create(bar);
    lv_label_set_text(lbl, (title != NULL) ? title : "");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, ui_font_text(UI_FONT_TITLE), LV_PART_MAIN);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(lbl, UI_SCREEN_W - 2 * UI_PAD - 40 - 60);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, ui_nav_can_go_back() ? 34 : 0, 0);

    /* Clock, so the time is never more than a glance away. */
    lv_obj_t *clock = lv_label_create(bar);
    char buf[12];
    svc_time_format_clock(buf, sizeof(buf));
    lv_label_set_text(clock, buf);
    lv_obj_set_style_text_color(clock, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(clock, ui_font(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(clock, LV_ALIGN_RIGHT_MID, 0, 0);

    return bar;
}

/* ------------------------------------------------------------ screen base */

static lv_obj_t *screen_base_common(const char *title, lv_obj_t **out_body)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_style_plain(scr);
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    ui_widgets_header(scr, title);
    ui_screen_enable_back_gesture(scr);

    lv_obj_t *body = lv_obj_create(scr);
    ui_style_plain(body);
    lv_obj_set_size(body, UI_SCREEN_W, UI_SCREEN_H - UI_HEADER_H);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, UI_HEADER_H);
    lv_obj_set_style_pad_all(body, UI_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, UI_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);

    /* Vertical scrolling only: horizontal drags belong to the back gesture. */
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);

    if (out_body != NULL) {
        *out_body = body;
    }
    return scr;
}

lv_obj_t *ui_screen_base(i18n_id_t title_id, lv_obj_t **out_body)
{
    return screen_base_common(i18n(title_id), out_body);
}

lv_obj_t *ui_screen_base_text(const char *title, lv_obj_t **out_body)
{
    return screen_base_common(title, out_body);
}

/* ------------------------------------------------------------------- cards */

lv_obj_t *ui_card(lv_obj_t *parent, lv_coord_t height)
{
    lv_obj_t *card = lv_obj_create(parent);
    ui_style_card(card);
    lv_obj_set_width(card, LV_PCT(100));
    if (height > 0) {
        lv_obj_set_height(card, height);
    } else {
        lv_obj_set_height(card, LV_SIZE_CONTENT);
    }
    return card;
}

lv_obj_t *ui_stat_tile(lv_obj_t *parent, const char *icon, lv_color_t icon_color,
                       const char *caption, const char *value, lv_obj_t **out_value)
{
    lv_obj_t *card = lv_obj_create(parent);
    ui_style_card(card);
    lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);

    lv_obj_t *ico = lv_label_create(card);
    lv_label_set_text(ico, (icon != NULL) ? icon : "");
    lv_obj_set_style_text_color(ico, icon_color, LV_PART_MAIN);
    lv_obj_set_style_text_font(ico, ui_font(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(ico, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *val = lv_label_create(card);
    lv_label_set_text(val, (value != NULL) ? value : "-");
    lv_obj_set_style_text_color(val, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(val, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
    lv_obj_align(val, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *cap = lv_label_create(card);
    lv_label_set_text(cap, (caption != NULL) ? caption : "");
    lv_obj_set_style_text_color(cap, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(cap, ui_font_text(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_align(cap, LV_ALIGN_TOP_RIGHT, 0, 2);

    if (out_value != NULL) {
        *out_value = val;
    }
    return card;
}

lv_obj_t *ui_kv_row(lv_obj_t *parent, const char *key, const char *value,
                    lv_obj_t **out_value)
{
    lv_obj_t *row = lv_obj_create(parent);
    ui_style_plain(row);
    lv_obj_set_size(row, LV_PCT(100), 30);

    lv_obj_t *k = lv_label_create(row);
    lv_label_set_text(k, (key != NULL) ? key : "");
    lv_obj_set_style_text_color(k, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(k, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *v = lv_label_create(row);
    lv_label_set_text(v, (value != NULL) ? value : "-");
    lv_obj_set_style_text_color(v, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(v, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(v, LV_PCT(60));
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);

    if (out_value != NULL) {
        *out_value = v;
    }
    return row;
}

/* -------------------------------------------------------------------- rows */

lv_obj_t *ui_menu_row(lv_obj_t *parent, const char *icon, lv_color_t icon_color,
                      const char *label, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *row = lv_obj_create(parent);
    ui_style_card(row);
    lv_obj_set_size(row, LV_PCT(100), 54);
    lv_obj_set_style_pad_hor(row, 12, LV_PART_MAIN);

    if (cb != NULL) {
        ui_style_card_pressable(row);
        lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user_data);
    }

    lv_coord_t text_x = 0;
    if (icon != NULL && icon[0] != '\0') {
        /* A rounded colour chip behind the glyph, as in the design set. */
        lv_obj_t *chip = lv_obj_create(row);
        ui_style_plain(chip);
        lv_obj_set_size(chip, 30, 30);
        lv_obj_set_style_radius(chip, 9, LV_PART_MAIN);
        lv_obj_set_style_bg_color(chip, icon_color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_align(chip, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *ico = lv_label_create(chip);
        lv_label_set_text(ico, icon);
        lv_obj_set_style_text_color(ico, UI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(ico, ui_font(UI_FONT_SMALL), LV_PART_MAIN);
        lv_obj_center(ico);

        text_x = 40;
    }

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, (label != NULL) ? label : "");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(lbl, UI_CONTENT_W - text_x - 50);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, text_x, 0);

    if (cb != NULL) {
        lv_obj_t *chev = lv_label_create(row);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chev, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
        lv_obj_set_style_text_font(chev, ui_font(UI_FONT_SMALL), LV_PART_MAIN);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    return row;
}

lv_obj_t *ui_toggle_row(lv_obj_t *parent, const char *label, bool state,
                        lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *row = lv_obj_create(parent);
    ui_style_card(row);
    lv_obj_set_size(row, LV_PCT(100), 54);
    lv_obj_set_style_pad_hor(row, 12, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, (label != NULL) ? label : "");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(lbl, UI_CONTENT_W - 90);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 48, 26);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x2A3546), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, UI_COLOR_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (state) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    if (cb != NULL) {
        lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, user_data);
    }
    return sw;   /* the switch, so callers can read it back */
}

/* ------------------------------------------------------------------- misc */

lv_obj_t *ui_pill(lv_obj_t *parent, const char *text, lv_color_t color)
{
    lv_obj_t *pill = lv_obj_create(parent);
    ui_style_plain(pill);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, 24);
    lv_obj_set_style_radius(pill, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pill, color, LV_PART_MAIN);
    /* Tinted rather than solid, so a row of pills does not shout. */
    lv_obj_set_style_bg_opa(pill, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(pill, 10, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(pill);
    lv_label_set_text(lbl, (text != NULL) ? text : "");
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_center(lbl);
    return pill;
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_color_t color,
                    lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    ui_style_button(btn, color);
    lv_obj_set_size(btn, LV_PCT(100), 42);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, (text != NULL) ? text : "");
    lv_obj_set_style_text_font(lbl, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_center(lbl);

    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

lv_obj_t *ui_empty_state(lv_obj_t *parent, const char *icon, const char *message,
                         const char *hint)
{
    lv_obj_t *box = lv_obj_create(parent);
    ui_style_plain(box);
    lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 10, LV_PART_MAIN);

    if (icon != NULL) {
        lv_obj_t *ico = lv_label_create(box);
        lv_label_set_text(ico, icon);
        lv_obj_set_style_text_color(ico, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
        lv_obj_set_style_text_font(ico, ui_font(UI_FONT_LARGE), LV_PART_MAIN);
    }

    lv_obj_t *msg = lv_label_create(box);
    lv_label_set_text(msg, (message != NULL) ? message : "");
    lv_obj_set_style_text_color(msg, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(msg, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(msg, UI_CONTENT_W - 20);

    if (hint != NULL && hint[0] != '\0') {
        lv_obj_t *h = lv_label_create(box);
        lv_label_set_text(h, hint);
        lv_obj_set_style_text_color(h, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
        lv_obj_set_style_text_font(h, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(h, UI_CONTENT_W - 20);
    }
    return box;
}

lv_obj_t *ui_ring(lv_obj_t *parent, lv_coord_t size, lv_color_t color,
                  int32_t value, const char *centre_text, lv_obj_t **out_label)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, value);

    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);   /* display only */
    lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x1E2A3A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    lv_obj_t *lbl = lv_label_create(arc);
    lv_label_set_text(lbl, (centre_text != NULL) ? centre_text : "");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
    lv_obj_center(lbl);

    if (out_label != NULL) {
        *out_label = lbl;
    }
    return arc;
}

/* --------------------------------------------------------- signal strength */

#define SIGNAL_BAR_COUNT 4

lv_obj_t *ui_signal_bars(lv_obj_t *parent, uint8_t bars, lv_color_t color)
{
    lv_obj_t *box = lv_obj_create(parent);
    ui_style_plain(box);
    lv_obj_set_size(box, 26, 18);

    for (int i = 0; i < SIGNAL_BAR_COUNT; i++) {
        lv_obj_t *bar = lv_obj_create(box);
        ui_style_plain(bar);
        const lv_coord_t h = (lv_coord_t)(5 + i * 4);
        lv_obj_set_size(bar, 4, h);
        lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, (lv_coord_t)(i * 6), 0);
    }
    ui_signal_bars_set(box, bars, color);
    return box;
}

void ui_signal_bars_set(lv_obj_t *obj, uint8_t bars, lv_color_t color)
{
    if (obj == NULL) {
        return;
    }
    const uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n && i < SIGNAL_BAR_COUNT; i++) {
        lv_obj_t *bar = lv_obj_get_child(obj, (int32_t)i);
        const bool lit = (i < bars);
        lv_obj_set_style_bg_color(bar, lit ? color : lv_color_hex(0x263041), LV_PART_MAIN);
    }
}
