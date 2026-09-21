/*
 * 21 - OBD2 home, 22 - Live data, 23 - Trouble codes,
 * 24 - Freeze frame, 25 - Vehicle status.
 *
 * The whole "Car Dangerous" section. One file because all five read the
 * same service and share the PID formatting and the not-connected path.
 *
 * Connecting blocks for several seconds (scan, link, ELM327 reset, protocol
 * negotiation), so it runs on a worker task rather than in a click handler -
 * doing it inline would freeze the UI for the duration.
 */
#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_obd2.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"

/* ---------------------------------------------------------------- shared */

static void connect_worker(void *arg)
{
    (void)arg;
    svc_obd2_connect();
    vTaskDelete(NULL);
}

static void start_connect(void)
{
    /* 6 KB: the ELM327 response buffer plus the BLE stack's call depth. */
    if (xTaskCreate(connect_worker, "obd2_conn", 6144, NULL, 4, NULL) != pdPASS) {
        ui_toast(i18n(STR_ERROR), UI_COLOR_RED);
    } else {
        ui_toast(i18n(STR_SEARCHING_ADAPTER), UI_COLOR_ACCENT);
    }
}

static bool obd2_ready(void)
{
    return svc_obd2_status()->state == SVC_OBD2_CONNECTED;
}

/* Renders one PID, or a dash when the ECU does not support it. */
static void set_pid_label(lv_obj_t *label, const svc_obd2_pid_t *pid,
                          const char *unit, int decimals)
{
    if (pid == NULL || !pid->valid) {
        lv_label_set_text(label, "-");
        return;
    }
    if (decimals == 0) {
        lv_label_set_text_fmt(label, "%d %s", (int)(pid->value + 0.5f), unit);
    } else {
        lv_label_set_text_fmt(label, "%.1f %s", (double)pid->value, unit);
    }
}

/* Shared "no adapter" panel with a connect button. */
static lv_obj_t *not_connected_panel(lv_obj_t *body, lv_event_cb_t on_connect)
{
    const svc_obd2_status_t *st = svc_obd2_status();
    const char *msg = (st->state == SVC_OBD2_SEARCHING) ? i18n(STR_SEARCHING_ADAPTER)
                    : (st->state == SVC_OBD2_CONNECTING) ? i18n(STR_CONNECTING)
                    : (st->state == SVC_OBD2_LINK_READY) ? st->last_error
                                                         : i18n(STR_NO_ADAPTER);

    lv_obj_t *box = ui_empty_state(body, LV_SYMBOL_DRIVE, msg,
                                   (st->last_error[0] != '\0' &&
                                    st->state != SVC_OBD2_LINK_READY)
                                   ? st->last_error : NULL);
    ui_button(body, i18n(STR_CONNECT), UI_COLOR_RED, on_connect, NULL);
    return box;
}

/* ======================================================== 21 obd2 home == */

static lv_obj_t *s_home_state;
static lv_obj_t *s_home_adapter;
static lv_obj_t *s_home_proto;
static lv_obj_t *s_home_mil;

static void on_home_row(lv_event_t *e)
{
    const ui_screen_id_t target = (ui_screen_id_t)(uintptr_t)lv_event_get_user_data(e);
    if (!obd2_ready()) {
        ui_toast(i18n(STR_NO_ADAPTER), UI_COLOR_ORANGE);
        return;
    }
    ui_nav_go(target);
}

static void on_home_connect(lv_event_t *e)
{
    (void)e;
    if (obd2_ready()) {
        svc_obd2_disconnect();
        ui_nav_rebuild();
    } else {
        start_connect();
    }
}

static void home_refresh(void)
{
    const svc_obd2_status_t *st = svc_obd2_status();
    if (s_home_state == NULL) {
        return;
    }

    const char *text;
    lv_color_t color;
    switch (st->state) {
    case SVC_OBD2_CONNECTED:  text = i18n(STR_CONNECTED);  color = UI_COLOR_GREEN;  break;
    case SVC_OBD2_LINK_READY: text = i18n(STR_ADAPTER);    color = UI_COLOR_ORANGE; break;
    case SVC_OBD2_CONNECTING: text = i18n(STR_CONNECTING); color = UI_COLOR_ACCENT; break;
    case SVC_OBD2_SEARCHING:  text = i18n(STR_SCANNING);   color = UI_COLOR_ACCENT; break;
    case SVC_OBD2_ERROR:      text = i18n(STR_ERROR);      color = UI_COLOR_RED;    break;
    default:                  text = i18n(STR_NOT_CONNECTED); color = UI_COLOR_TEXT_DIM; break;
    }
    lv_label_set_text(s_home_state, text);
    lv_obj_set_style_text_color(s_home_state, color, LV_PART_MAIN);

    lv_label_set_text(s_home_adapter,
                      (st->adapter_name[0] != '\0') ? st->adapter_name : i18n(STR_NONE));
    lv_label_set_text(s_home_proto,
                      (st->protocol[0] != '\0') ? st->protocol : "-");

    if (st->state == SVC_OBD2_CONNECTED) {
        lv_label_set_text_fmt(s_home_mil, "%s: %u",
                              i18n(STR_TROUBLE_CODES), (unsigned)st->dtc_count);
        lv_obj_set_style_text_color(s_home_mil,
                                    st->mil_on ? UI_COLOR_RED : UI_COLOR_GREEN,
                                    LV_PART_MAIN);
    } else {
        lv_label_set_text(s_home_mil, "-");
    }
}

static void on_obd2_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id != WATCH_EV_OBD2_STATE || !ui_nav_is_current(UI_SCR_OBD2_HOME)) {
        return;
    }
    if (bsp_display_lock(50)) {
        home_refresh();
        bsp_display_unlock();
    }
}

lv_obj_t *scr_obd2_home_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_OBD2_TITLE, &body);

    /* ---- banner ---- */
    lv_obj_t *banner = ui_card(body, 92);
    lv_obj_set_style_border_color(banner, UI_COLOR_RED, LV_PART_MAIN);

    lv_obj_t *car = lv_label_create(banner);
    lv_label_set_text(car, LV_SYMBOL_DRIVE);
    lv_obj_set_style_text_color(car, UI_COLOR_RED, LV_PART_MAIN);
    lv_obj_set_style_text_font(car, ui_font(UI_FONT_LARGE), LV_PART_MAIN);
    lv_obj_align(car, LV_ALIGN_LEFT_MID, 2, 0);

    lv_obj_t *title = lv_label_create(banner);
    lv_label_set_text(title, "OBD2");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(title, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 62, -18);

    s_home_state = lv_label_create(banner);
    lv_label_set_text(s_home_state, "");
    lv_obj_set_style_text_font(s_home_state, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(s_home_state, LV_ALIGN_LEFT_MID, 62, 2);

    s_home_mil = lv_label_create(banner);
    lv_label_set_text(s_home_mil, "-");
    lv_obj_set_style_text_color(s_home_mil, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_home_mil, ui_font_text(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_align(s_home_mil, LV_ALIGN_LEFT_MID, 62, 22);

    /* ---- adapter detail ---- */
    lv_obj_t *detail = ui_card(body, 0);
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
    ui_kv_row(detail, i18n(STR_ADAPTER), "-", &s_home_adapter);
    ui_kv_row(detail, "Protocol", "-", &s_home_proto);

    /* ---- menu ---- */
    ui_menu_row(body, LV_SYMBOL_CHARGE, UI_COLOR_GREEN, i18n(STR_LIVE_DATA),
                on_home_row, (void *)(uintptr_t)UI_SCR_OBD2_LIVE);
    ui_menu_row(body, LV_SYMBOL_WARNING, UI_COLOR_RED, i18n(STR_TROUBLE_CODES),
                on_home_row, (void *)(uintptr_t)UI_SCR_OBD2_DTC);
    ui_menu_row(body, LV_SYMBOL_IMAGE, UI_COLOR_CYAN, i18n(STR_FREEZE_FRAME),
                on_home_row, (void *)(uintptr_t)UI_SCR_OBD2_FREEZE);
    ui_menu_row(body, LV_SYMBOL_OK, UI_COLOR_ACCENT, i18n(STR_VEHICLE_STATUS),
                on_home_row, (void *)(uintptr_t)UI_SCR_OBD2_STATUS);

    ui_button(body, obd2_ready() ? i18n(STR_DISCONNECT) : i18n(STR_CONNECT),
              obd2_ready() ? lv_color_hex(0x1B2636) : UI_COLOR_RED,
              on_home_connect, NULL);

    lv_obj_t *note = lv_label_create(body);
    lv_label_set_text(note, "BLE ELM327 adapters only - the ESP32-S3 has no "
                            "Bluetooth Classic radio.");
    lv_obj_set_style_text_color(note, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(note, ui_font(UI_FONT_TINY), LV_PART_MAIN);
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(note, LV_PCT(100));

    home_refresh();
    ui_screen_subscribe(scr, on_obd2_event);
    return scr;
}

/* ======================================================== 22 live data == */

typedef struct {
    lv_obj_t *value;
    lv_obj_t *bar;
} live_tile_t;

static live_tile_t s_live_load;
static live_tile_t s_live_coolant;
static live_tile_t s_live_rpm;
static live_tile_t s_live_speed;
static live_tile_t s_live_intake;
static live_tile_t s_live_fuel;
static lv_obj_t   *s_live_volts;

static void live_tile(lv_obj_t *parent, const char *icon, lv_color_t color,
                      const char *caption, live_tile_t *out)
{
    lv_obj_t *card = ui_card(parent, 86);
    lv_obj_set_width(card, (UI_CONTENT_W - UI_GAP) / 2);
    lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);

    lv_obj_t *ico = lv_label_create(card);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_color(ico, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(ico, ui_font(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(ico, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *cap = lv_label_create(card);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(cap, ui_font_text(UI_FONT_TINY), LV_PART_MAIN);
    lv_label_set_long_mode(cap, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(cap, 110);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 22, 1);

    out->value = lv_label_create(card);
    lv_label_set_text(out->value, "-");
    lv_obj_set_style_text_color(out->value, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(out->value, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
    lv_obj_align(out->value, LV_ALIGN_LEFT_MID, 0, 6);

    out->bar = lv_bar_create(card);
    lv_obj_set_size(out->bar, LV_PCT(100), 4);
    lv_obj_align(out->bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(out->bar, 0, 100);
    lv_bar_set_value(out->bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(out->bar, lv_color_hex(0x1E2A3A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(out->bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_radius(out->bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(out->bar, 2, LV_PART_INDICATOR);
}

static void live_set(live_tile_t *tile, const svc_obd2_pid_t *pid,
                     const char *unit, int decimals, float full_scale)
{
    if (tile->value == NULL) {
        return;
    }
    set_pid_label(tile->value, pid, unit, decimals);

    int32_t pct = 0;
    if (pid != NULL && pid->valid && full_scale > 0.0f) {
        pct = (int32_t)((pid->value / full_scale) * 100.0f);
        if (pct < 0)   { pct = 0; }
        if (pct > 100) { pct = 100; }
    }
    lv_bar_set_value(tile->bar, pct, LV_ANIM_ON);
}

static void live_refresh(void)
{
    const svc_obd2_live_t *l = svc_obd2_live();

    live_set(&s_live_load,    &l->engine_load,  "%",    0, 100.0f);
    live_set(&s_live_coolant, &l->coolant_temp, "\xC2\xB0""C", 0, 120.0f);
    live_set(&s_live_rpm,     &l->rpm,          "rpm",  0, 7000.0f);
    live_set(&s_live_speed,   &l->speed,        "km/h", 0, 180.0f);
    live_set(&s_live_intake,  &l->intake_temp,  "\xC2\xB0""C", 0, 80.0f);
    live_set(&s_live_fuel,    &l->fuel_level,   "%",    0, 100.0f);

    if (s_live_volts != NULL) {
        set_pid_label(s_live_volts, &l->battery_voltage, "V", 1);
    }
}

static void on_live_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id != WATCH_EV_OBD2_LIVE || !ui_nav_is_current(UI_SCR_OBD2_LIVE)) {
        return;
    }
    if (bsp_display_lock(50)) {
        live_refresh();
        bsp_display_unlock();
    }
}

static void on_live_deleted(lv_event_t *e)
{
    (void)e;
    /* Polling keeps the BLE link saturated; stop it the moment the screen
     * that needs it goes away. */
    svc_obd2_set_polling(false);
    memset(&s_live_load, 0, sizeof(s_live_load));
    memset(&s_live_coolant, 0, sizeof(s_live_coolant));
    memset(&s_live_rpm, 0, sizeof(s_live_rpm));
    memset(&s_live_speed, 0, sizeof(s_live_speed));
    memset(&s_live_intake, 0, sizeof(s_live_intake));
    memset(&s_live_fuel, 0, sizeof(s_live_fuel));
    s_live_volts = NULL;
}

static void on_live_connect(lv_event_t *e) { (void)e; start_connect(); }

lv_obj_t *scr_obd2_live_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_LIVE_DATA, &body);

    if (!obd2_ready()) {
        not_connected_panel(body, on_live_connect);
        ui_screen_subscribe(scr, on_live_event);
        return scr;
    }

    lv_obj_t *grid = lv_obj_create(body);
    ui_style_plain(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, UI_GAP, LV_PART_MAIN);

    live_tile(grid, LV_SYMBOL_CHARGE, UI_COLOR_GREEN,  i18n(STR_ENGINE_LOAD),   &s_live_load);
    live_tile(grid, LV_SYMBOL_TINT,   UI_COLOR_ORANGE, i18n(STR_COOLANT_TEMP),  &s_live_coolant);
    live_tile(grid, LV_SYMBOL_LOOP,   UI_COLOR_PURPLE, i18n(STR_RPM),           &s_live_rpm);
    live_tile(grid, LV_SYMBOL_GPS,    UI_COLOR_CYAN,   i18n(STR_VEHICLE_SPEED), &s_live_speed);
    live_tile(grid, LV_SYMBOL_DOWNLOAD, UI_COLOR_ACCENT, i18n(STR_INTAKE_TEMP), &s_live_intake);
    live_tile(grid, LV_SYMBOL_BATTERY_3, UI_COLOR_PINK, i18n(STR_FUEL_LEVEL),   &s_live_fuel);

    lv_obj_t *volts = ui_card(body, 0);
    lv_obj_set_flex_flow(volts, LV_FLEX_FLOW_COLUMN);
    ui_kv_row(volts, i18n(STR_BATTERY), "-", &s_live_volts);

    lv_obj_add_event_cb(scr, on_live_deleted, LV_EVENT_DELETE, NULL);
    svc_obd2_set_polling(true);
    live_refresh();
    ui_screen_subscribe(scr, on_live_event);
    return scr;
}

/* ==================================================== 23 trouble codes == */

static lv_obj_t *s_dtc_list;
static lv_obj_t *s_dtc_summary;

static void dtc_fill(void)
{
    lv_obj_clean(s_dtc_list);

    svc_obd2_dtc_t codes[SVC_OBD2_MAX_DTC];
    const size_t n = svc_obd2_get_dtcs(codes, SVC_OBD2_MAX_DTC);

    lv_label_set_text_fmt(s_dtc_summary, "%s: %u", i18n(STR_TROUBLE_CODES),
                          (unsigned)n);
    lv_obj_set_style_text_color(s_dtc_summary,
                                (n > 0) ? UI_COLOR_RED : UI_COLOR_GREEN,
                                LV_PART_MAIN);

    if (n == 0) {
        /* Not ui_empty_state() here: that sizes itself to 100% of its
         * parent, and this list is LV_SIZE_CONTENT, which would collapse
         * it to nothing. */
        lv_obj_t *card = ui_card(s_dtc_list, 72);
        lv_obj_t *ico = lv_label_create(card);
        lv_label_set_text(ico, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(ico, UI_COLOR_GREEN, LV_PART_MAIN);
        lv_obj_set_style_text_font(ico, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
        lv_obj_align(ico, LV_ALIGN_CENTER, 0, -12);

        lv_obj_t *lbl = lv_label_create(card);
        lv_label_set_text(lbl, i18n(STR_NO_CODES));
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 14);
        return;
    }

    for (size_t i = 0; i < n; i++) {
        const bool pending = codes[i].pending;
        const lv_color_t c = pending ? UI_COLOR_ORANGE : UI_COLOR_RED;

        lv_obj_t *row = ui_card(s_dtc_list, 62);
        lv_obj_set_style_pad_hor(row, 12, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, c, LV_PART_MAIN);

        lv_obj_t *ico = lv_label_create(row);
        lv_label_set_text(ico, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(ico, c, LV_PART_MAIN);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *code = lv_label_create(row);
        lv_label_set_text(code, codes[i].code);
        lv_obj_set_style_text_color(code, UI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(code, ui_font(UI_FONT_BODY), LV_PART_MAIN);
        lv_obj_align(code, LV_ALIGN_LEFT_MID, 28, -11);

        const char *desc = svc_obd2_describe_dtc(codes[i].code);
        lv_obj_t *text = lv_label_create(row);
        lv_label_set_text(text, (desc != NULL) ? desc : i18n(STR_UNKNOWN));
        lv_obj_set_style_text_color(text, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(text, ui_font(UI_FONT_TINY), LV_PART_MAIN);
        lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(text, UI_CONTENT_W - 60);
        lv_obj_align(text, LV_ALIGN_LEFT_MID, 28, 10);

        if (pending) {
            lv_obj_t *tag = ui_pill(row, i18n(STR_PENDING), UI_COLOR_ORANGE);
            lv_obj_align(tag, LV_ALIGN_RIGHT_MID, 0, -11);
        }
    }
}

/* Both of these are multi-second ELM327 exchanges, so neither may run on
 * the LVGL task. The read announces itself through WATCH_EV_OBD2_DTC, which
 * the screen already listens for. */
static void dtc_read_worker(void *arg)
{
    (void)arg;
    svc_obd2_read_dtcs();
    vTaskDelete(NULL);
}

static void dtc_clear_worker(void *arg)
{
    (void)arg;
    if (svc_obd2_clear_dtcs() == ESP_OK) {
        ui_toast(i18n(STR_CLEAR_CODES), UI_COLOR_GREEN);
        /* Re-read so the list reflects what the ECU now reports rather
         * than what we hope it does. */
        svc_obd2_read_dtcs();
    } else {
        ui_toast(i18n(STR_ERROR), UI_COLOR_RED);
    }
    vTaskDelete(NULL);
}

static void on_dtc_read(lv_event_t *e)
{
    (void)e;
    if (!obd2_ready()) {
        ui_toast(i18n(STR_NO_ADAPTER), UI_COLOR_ORANGE);
        return;
    }
    if (xTaskCreate(dtc_read_worker, "obd2_dtc", 5120, NULL, 4, NULL) != pdPASS) {
        ui_toast(i18n(STR_ERROR), UI_COLOR_RED);
    }
}

static void on_dtc_clear(lv_event_t *e)
{
    (void)e;
    if (!obd2_ready()) {
        return;
    }
    if (xTaskCreate(dtc_clear_worker, "obd2_clr", 5120, NULL, 4, NULL) != pdPASS) {
        ui_toast(i18n(STR_ERROR), UI_COLOR_RED);
    }
}

static void on_dtc_connect(lv_event_t *e) { (void)e; start_connect(); }

static void on_dtc_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id != WATCH_EV_OBD2_DTC || !ui_nav_is_current(UI_SCR_OBD2_DTC)) {
        return;
    }
    if (bsp_display_lock(100)) {
        dtc_fill();
        bsp_display_unlock();
    }
}

lv_obj_t *scr_obd2_dtc_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_TROUBLE_CODES, &body);

    if (!obd2_ready()) {
        not_connected_panel(body, on_dtc_connect);
        return scr;
    }

    s_dtc_summary = lv_label_create(body);
    lv_label_set_text(s_dtc_summary, "");
    lv_obj_set_style_text_font(s_dtc_summary, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);

    s_dtc_list = lv_obj_create(body);
    ui_style_plain(s_dtc_list);
    lv_obj_set_width(s_dtc_list, LV_PCT(100));
    lv_obj_set_height(s_dtc_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_dtc_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_dtc_list, 8, LV_PART_MAIN);

    ui_button(body, i18n(STR_RETRY), lv_color_hex(0x1B2636), on_dtc_read, NULL);
    ui_button(body, i18n(STR_CLEAR_CODES), UI_COLOR_RED, on_dtc_clear, NULL);

    lv_obj_t *warn = lv_label_create(body);
    lv_label_set_text(warn, "Clearing also resets the readiness monitors. "
                            "They need a full drive cycle to re-run.");
    lv_obj_set_style_text_color(warn, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(warn, ui_font(UI_FONT_TINY), LV_PART_MAIN);
    lv_label_set_long_mode(warn, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(warn, LV_PCT(100));

    dtc_fill();
    ui_screen_subscribe(scr, on_dtc_event);
    return scr;
}

/* ===================================================== 24 freeze frame == */

static void on_freeze_connect(lv_event_t *e) { (void)e; start_connect(); }

/*
 * Reading a freeze frame is seven ELM327 round trips and can take several
 * seconds. Doing that inside the create function would block the LVGL task
 * and freeze the whole watch, so it runs on a worker and the screen shows a
 * spinner until the result lands.
 */
typedef enum { READ_PENDING = 0, READ_OK, READ_EMPTY } read_state_t;

static volatile read_state_t s_freeze_state;
static svc_obd2_live_t       s_freeze_data;
static lv_obj_t             *s_freeze_body;
static lv_obj_t             *s_freeze_spinner;

static void freeze_worker(void *arg)
{
    (void)arg;
    svc_obd2_live_t frame;
    const esp_err_t err = svc_obd2_read_freeze_frame(0, &frame);
    if (err == ESP_OK) {
        s_freeze_data = frame;
        s_freeze_state = READ_OK;
    } else {
        s_freeze_state = READ_EMPTY;
    }
    vTaskDelete(NULL);
}

static void freeze_build(lv_obj_t *body, const svc_obd2_live_t *frame);

static void freeze_tick(lv_timer_t *t)
{
    if (s_freeze_state == READ_PENDING || s_freeze_body == NULL) {
        return;
    }
    lv_timer_delete(t);   /* nothing more to wait for */

    if (s_freeze_spinner != NULL) {
        lv_obj_delete(s_freeze_spinner);
        s_freeze_spinner = NULL;
    }

    if (s_freeze_state == READ_EMPTY) {
        ui_empty_state(s_freeze_body, LV_SYMBOL_IMAGE, i18n(STR_NO_FREEZE_FRAME),
                       "The ECU has not stored a snapshot.");
    } else {
        freeze_build(s_freeze_body, &s_freeze_data);
    }
    s_freeze_body = NULL;
}

static void on_freeze_deleted(lv_event_t *e)
{
    (void)e;
    s_freeze_body = NULL;
    s_freeze_spinner = NULL;
}

lv_obj_t *scr_obd2_freeze_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_FREEZE_FRAME, &body);

    if (!obd2_ready()) {
        not_connected_panel(body, on_freeze_connect);
        return scr;
    }

    s_freeze_body = body;
    s_freeze_state = READ_PENDING;

    s_freeze_spinner = lv_spinner_create(body);
    lv_obj_set_size(s_freeze_spinner, 44, 44);
    lv_obj_set_style_arc_width(s_freeze_spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_freeze_spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_freeze_spinner, lv_color_hex(0x1B2636), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_freeze_spinner, UI_COLOR_ORANGE, LV_PART_INDICATOR);

    if (xTaskCreate(freeze_worker, "obd2_ff", 5120, NULL, 4, NULL) != pdPASS) {
        s_freeze_state = READ_EMPTY;
    }

    lv_obj_add_event_cb(scr, on_freeze_deleted, LV_EVENT_DELETE, NULL);
    ui_screen_add_timer(scr, freeze_tick, 200);
    return scr;
}

static void freeze_build(lv_obj_t *body, const svc_obd2_live_t *frame)
{
    /* The DTC the frame was captured against, if the ECU reported one. */
    svc_obd2_dtc_t codes[SVC_OBD2_MAX_DTC];
    const size_t n = svc_obd2_get_dtcs(codes, SVC_OBD2_MAX_DTC);

    lv_obj_t *head = ui_card(body, 64);
    lv_obj_set_style_border_color(head, UI_COLOR_ORANGE, LV_PART_MAIN);

    lv_obj_t *ico = lv_label_create(head);
    lv_label_set_text(ico, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(ico, UI_COLOR_ORANGE, LV_PART_MAIN);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *code = lv_label_create(head);
    lv_label_set_text(code, (n > 0) ? codes[0].code : i18n(STR_FREEZE_FRAME));
    lv_obj_set_style_text_color(code, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(code, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
    lv_obj_align(code, LV_ALIGN_LEFT_MID, 28, -8);

    lv_obj_t *sub = lv_label_create(head);
    lv_label_set_text(sub, (n > 0 && svc_obd2_describe_dtc(codes[0].code) != NULL)
                           ? svc_obd2_describe_dtc(codes[0].code)
                           : "Conditions when the fault was stored");
    lv_obj_set_style_text_color(sub, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(sub, ui_font(UI_FONT_TINY), LV_PART_MAIN);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(sub, UI_CONTENT_W - 50);
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, 28, 14);

    lv_obj_t *card = ui_card(body, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *v;
    ui_kv_row(card, i18n(STR_ENGINE_LOAD), "-", &v);
    set_pid_label(v, &frame->engine_load, "%", 0);
    ui_kv_row(card, i18n(STR_RPM), "-", &v);
    set_pid_label(v, &frame->rpm, "rpm", 0);
    ui_kv_row(card, i18n(STR_VEHICLE_SPEED), "-", &v);
    set_pid_label(v, &frame->speed, "km/h", 0);
    ui_kv_row(card, i18n(STR_COOLANT_TEMP), "-", &v);
    set_pid_label(v, &frame->coolant_temp, "\xC2\xB0""C", 0);
    ui_kv_row(card, i18n(STR_INTAKE_TEMP), "-", &v);
    set_pid_label(v, &frame->intake_temp, "\xC2\xB0""C", 0);
    ui_kv_row(card, i18n(STR_THROTTLE), "-", &v);
    set_pid_label(v, &frame->throttle, "%", 0);
    ui_kv_row(card, i18n(STR_FUEL_LEVEL), "-", &v);
    set_pid_label(v, &frame->fuel_level, "%", 0);
}

/* =================================================== 25 vehicle status == */

static void on_status_connect(lv_event_t *e) { (void)e; start_connect(); }

static void status_row(lv_obj_t *parent, const char *icon, const char *label,
                       svc_obd2_monitor_t state)
{
    const char *text;
    lv_color_t color;

    switch (state) {
    case SVC_OBD2_MON_OK:
        text = i18n(STR_STATUS_OK);      color = UI_COLOR_GREEN;  break;
    case SVC_OBD2_MON_INCOMPLETE:
        text = i18n(STR_STATUS_WARNING); color = UI_COLOR_ORANGE; break;
    case SVC_OBD2_MON_FAULT:
        text = i18n(STR_STATUS_FAULT);   color = UI_COLOR_RED;    break;
    case SVC_OBD2_MON_UNSUPPORTED:
    default:
        text = i18n(STR_NOT_AVAILABLE);  color = UI_COLOR_TEXT_FAINT; break;
    }

    lv_obj_t *row = ui_card(parent, 54);
    lv_obj_set_style_pad_hor(row, 12, LV_PART_MAIN);

    lv_obj_t *ico = lv_label_create(row);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_color(ico, color, LV_PART_MAIN);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(l, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 30, 0);

    lv_obj_t *pill = ui_pill(row, text, color);
    lv_obj_align(pill, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* Same story as the freeze frame: the monitor read is a blocking ELM327
 * exchange, so it happens off the LVGL task. */
static volatile read_state_t s_mon_state;
static svc_obd2_monitors_t   s_mon_data;
static lv_obj_t             *s_mon_body;
static lv_obj_t             *s_mon_spinner;

static void monitors_worker(void *arg)
{
    (void)arg;
    svc_obd2_monitors_t mon;
    if (svc_obd2_read_monitors(&mon) == ESP_OK) {
        s_mon_data = mon;
        s_mon_state = READ_OK;
    } else {
        s_mon_state = READ_EMPTY;
    }
    vTaskDelete(NULL);
}

static void status_build(lv_obj_t *body, const svc_obd2_monitors_t *mon);

static void status_tick(lv_timer_t *t)
{
    if (s_mon_state == READ_PENDING || s_mon_body == NULL) {
        return;
    }
    lv_timer_delete(t);

    if (s_mon_spinner != NULL) {
        lv_obj_delete(s_mon_spinner);
        s_mon_spinner = NULL;
    }

    if (s_mon_state == READ_EMPTY) {
        ui_empty_state(s_mon_body, LV_SYMBOL_WARNING, i18n(STR_NOT_AVAILABLE),
                       "The ECU did not report its readiness monitors.");
    } else {
        status_build(s_mon_body, &s_mon_data);
    }
    s_mon_body = NULL;
}

static void on_status_deleted(lv_event_t *e)
{
    (void)e;
    s_mon_body = NULL;
    s_mon_spinner = NULL;
}

lv_obj_t *scr_obd2_status_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_VEHICLE_STATUS, &body);

    if (!obd2_ready()) {
        not_connected_panel(body, on_status_connect);
        return scr;
    }

    s_mon_body = body;
    s_mon_state = READ_PENDING;

    s_mon_spinner = lv_spinner_create(body);
    lv_obj_set_size(s_mon_spinner, 44, 44);
    lv_obj_set_style_arc_width(s_mon_spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_mon_spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_mon_spinner, lv_color_hex(0x1B2636), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_mon_spinner, UI_COLOR_ACCENT, LV_PART_INDICATOR);

    if (xTaskCreate(monitors_worker, "obd2_mon", 5120, NULL, 4, NULL) != pdPASS) {
        s_mon_state = READ_EMPTY;
    }

    lv_obj_add_event_cb(scr, on_status_deleted, LV_EVENT_DELETE, NULL);
    ui_screen_add_timer(scr, status_tick, 200);
    return scr;
}

static void status_build(lv_obj_t *body, const svc_obd2_monitors_t *mon)
{
    const svc_obd2_status_t *st = svc_obd2_status();

    /* ---- headline ---- */
    lv_obj_t *head = ui_card(body, 76);
    const bool trouble = st->mil_on || st->dtc_count > 0;
    lv_obj_set_style_border_color(head, trouble ? UI_COLOR_RED : UI_COLOR_GREEN,
                                  LV_PART_MAIN);

    lv_obj_t *ico = lv_label_create(head);
    lv_label_set_text(ico, trouble ? LV_SYMBOL_WARNING : LV_SYMBOL_OK);
    lv_obj_set_style_text_color(ico, trouble ? UI_COLOR_RED : UI_COLOR_GREEN,
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(ico, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t *l = lv_label_create(head);
    lv_label_set_text(l, trouble ? i18n(STR_STATUS_WARNING) : i18n(STR_DRIVE_SAFER));
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(l, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 44, -10);

    lv_obj_t *sub = lv_label_create(head);
    lv_label_set_text_fmt(sub, "%s: %u   MIL: %s", i18n(STR_TROUBLE_CODES),
                          (unsigned)st->dtc_count, st->mil_on ? "ON" : "OFF");
    lv_obj_set_style_text_color(sub, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(sub, ui_font_text(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, 44, 12);

    /* ---- systems ---- */
    status_row(body, LV_SYMBOL_CHARGE, i18n(STR_ENGINE),       mon->misfire);
    status_row(body, LV_SYMBOL_TINT,   i18n(STR_FUEL_LEVEL),   mon->fuel_system);
    status_row(body, LV_SYMBOL_SETTINGS, i18n(STR_SENSORS),    mon->components);
    status_row(body, LV_SYMBOL_REFRESH, "Catalyst",            mon->catalyst);
    status_row(body, LV_SYMBOL_EYE_OPEN, "O2 Sensor",          mon->oxygen_sensor);
    status_row(body, LV_SYMBOL_LOOP,   "EGR",                  mon->egr_system);

    lv_obj_t *note = lv_label_create(body);
    lv_label_set_text(note, "Readiness monitors, from mode 01 PID 01. "
                            "ABS, SRS and transmission are on manufacturer-specific "
                            "buses that generic OBD2 cannot reach.");
    lv_obj_set_style_text_color(note, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(note, ui_font(UI_FONT_TINY), LV_PART_MAIN);
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(note, LV_PCT(100));
}
