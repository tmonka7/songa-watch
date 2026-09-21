/*
 * 08 - Wi-Fi, 09 - Bluetooth, 17 - Network settings.
 *
 * All three are scan-and-pick lists over a radio that may be off, so they
 * share the same shape: a state card at the top, a live list below, and a
 * handler that only touches widgets while its own screen is still current.
 */
#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_ble.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_settings.h"
#include "watch_svc/svc_wifi.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "bsp/esp-bsp.h"

/* ============================================================ 08 wi-fi == */

static lv_obj_t *s_wifi_state;
static lv_obj_t *s_wifi_ssid;
static lv_obj_t *s_wifi_ip;
static lv_obj_t *s_wifi_rssi;
static lv_obj_t *s_wifi_bars;
static lv_obj_t *s_wifi_btn;

/* The password the user is typing, kept while the keyboard is open. */
static char s_pending_ssid[WATCH_SSID_MAX_LEN];

static const char *wifi_state_text(svc_wifi_state_t st)
{
    switch (st) {
    case SVC_WIFI_CONNECTED:    return i18n(STR_CONNECTED);
    case SVC_WIFI_CONNECTING:   return i18n(STR_CONNECTING);
    case SVC_WIFI_FAILED:       return i18n(STR_ERROR);
    case SVC_WIFI_DISCONNECTED: return i18n(STR_NOT_CONNECTED);
    case SVC_WIFI_OFF:
    default:                    return i18n(STR_OFF);
    }
}

static void wifi_refresh(void)
{
    const svc_wifi_status_t *st = svc_wifi_status();

    lv_label_set_text(s_wifi_state, wifi_state_text(st->state));
    lv_obj_set_style_text_color(s_wifi_state,
                                (st->state == SVC_WIFI_CONNECTED) ? UI_COLOR_GREEN
                                                                  : UI_COLOR_TEXT_DIM,
                                LV_PART_MAIN);

    lv_label_set_text(s_wifi_ssid, (st->ssid[0] != '\0') ? st->ssid : i18n(STR_NONE));
    lv_label_set_text(s_wifi_ip, (st->ip[0] != '\0') ? st->ip : "-");

    if (st->state == SVC_WIFI_CONNECTED) {
        lv_label_set_text_fmt(s_wifi_rssi, "%d dBm", (int)st->rssi);
    } else {
        lv_label_set_text(s_wifi_rssi, "-");
    }
    ui_signal_bars_set(s_wifi_bars, st->bars, UI_COLOR_CYAN);
}

static void on_wifi_toggle(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    svc_wifi_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void on_wifi_scan(lv_event_t *e)
{
    (void)e;
    ui_nav_go(UI_SCR_NETWORK);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id != WATCH_EV_WIFI_STATE) {
        return;
    }
    /* The screen may already be gone by the time this runs. */
    if (!ui_nav_is_current(UI_SCR_WIFI)) {
        return;
    }
    if (bsp_display_lock(50)) {
        wifi_refresh();
        bsp_display_unlock();
    }
}

lv_obj_t *scr_wifi_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_WIFI, &body);

    lv_obj_t *card = ui_card(body, 108);

    lv_obj_t *chip = lv_obj_create(card);
    ui_style_plain(chip);
    lv_obj_set_size(chip, 44, 44);
    lv_obj_set_style_radius(chip, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(chip, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *ico = lv_label_create(chip);
    lv_label_set_text(ico, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(ico, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_center(ico);

    s_wifi_state = lv_label_create(card);
    lv_label_set_text(s_wifi_state, "-");
    lv_obj_set_style_text_font(s_wifi_state, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(s_wifi_state, LV_ALIGN_TOP_LEFT, 56, 2);

    s_wifi_ssid = lv_label_create(card);
    lv_label_set_text(s_wifi_ssid, "-");
    lv_obj_set_style_text_color(s_wifi_ssid, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_wifi_ssid, ui_font(UI_FONT_SMALL), LV_PART_MAIN);
    lv_label_set_long_mode(s_wifi_ssid, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_wifi_ssid, 200);
    lv_obj_align(s_wifi_ssid, LV_ALIGN_TOP_LEFT, 56, 24);

    s_wifi_bars = ui_signal_bars(card, 0, UI_COLOR_CYAN);
    lv_obj_align(s_wifi_bars, LV_ALIGN_TOP_RIGHT, 0, 12);

    lv_obj_t *rows = lv_obj_create(card);
    ui_style_plain(rows);
    lv_obj_set_size(rows, LV_PCT(100), 34);
    lv_obj_align(rows, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
    ui_kv_row(rows, i18n(STR_IP_ADDRESS), "-", &s_wifi_ip);

    lv_obj_t *detail = ui_card(body, 0);
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
    ui_kv_row(detail, i18n(STR_SIGNAL), "-", &s_wifi_rssi);

    ui_toggle_row(body, i18n(STR_WIFI), svc_wifi_status()->state != SVC_WIFI_OFF,
                  on_wifi_toggle, NULL);

    s_wifi_btn = ui_button(body, i18n(STR_SCAN_NETWORKS), UI_COLOR_ACCENT,
                           on_wifi_scan, NULL);

    wifi_refresh();
    ui_screen_subscribe(scr, on_wifi_event);
    return scr;
}

/* =================================================== 17 network settings == */

static lv_obj_t *s_net_list;
static lv_obj_t *s_net_spinner;
static lv_obj_t *s_pw_modal;
static lv_obj_t *s_pw_input;

static void pw_close(void)
{
    if (s_pw_modal != NULL) {
        lv_obj_delete(s_pw_modal);
        s_pw_modal = NULL;
        s_pw_input = NULL;
    }
}

static void on_pw_cancel(lv_event_t *e)
{
    (void)e;
    pw_close();
}

static void on_pw_connect(lv_event_t *e)
{
    (void)e;
    const char *pw = (s_pw_input != NULL) ? lv_textarea_get_text(s_pw_input) : "";
    svc_wifi_connect(s_pending_ssid, pw);
    pw_close();
    ui_toast(i18n(STR_CONNECTING), UI_COLOR_ACCENT);
}

static void on_kb_event(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        on_pw_connect(e);
    } else if (code == LV_EVENT_CANCEL) {
        pw_close();
    }
}

/* An open network needs no password, so tapping it just connects. */
static void open_password_modal(const char *ssid, bool secured)
{
    strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
    s_pending_ssid[sizeof(s_pending_ssid) - 1] = '\0';

    if (!secured) {
        svc_wifi_connect(s_pending_ssid, "");
        ui_toast(i18n(STR_CONNECTING), UI_COLOR_ACCENT);
        return;
    }

    pw_close();

    /* On the top layer so the keyboard is never clipped by the scrolling
     * body underneath. */
    s_pw_modal = lv_obj_create(lv_layer_top());
    ui_style_plain(s_pw_modal);
    lv_obj_set_size(s_pw_modal, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(s_pw_modal, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pw_modal, LV_OPA_90, LV_PART_MAIN);
    lv_obj_add_flag(s_pw_modal, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(s_pw_modal);
    lv_label_set_text(title, s_pending_ssid);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(title, ui_font(UI_FONT_BODY), LV_PART_MAIN);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title, UI_CONTENT_W);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    s_pw_input = lv_textarea_create(s_pw_modal);
    lv_obj_set_size(s_pw_input, UI_CONTENT_W, 44);
    lv_obj_align(s_pw_input, LV_ALIGN_TOP_MID, 0, 48);
    lv_textarea_set_one_line(s_pw_input, true);
    lv_textarea_set_password_mode(s_pw_input, true);
    lv_textarea_set_placeholder_text(s_pw_input, i18n(STR_PASSWORD));
    lv_obj_set_style_bg_color(s_pw_input, UI_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_pw_input, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_pw_input, UI_COLOR_TEXT, LV_PART_MAIN);

    lv_obj_t *cancel = ui_button(s_pw_modal, i18n(STR_CANCEL),
                                 lv_color_hex(0x1B2636), on_pw_cancel, NULL);
    lv_obj_set_size(cancel, (UI_CONTENT_W - 8) / 2, 36);
    lv_obj_align(cancel, LV_ALIGN_TOP_LEFT, UI_PAD, 102);

    lv_obj_t *connect = ui_button(s_pw_modal, i18n(STR_CONNECT),
                                  UI_COLOR_ACCENT, on_pw_connect, NULL);
    lv_obj_set_size(connect, (UI_CONTENT_W - 8) / 2, 36);
    lv_obj_align(connect, LV_ALIGN_TOP_RIGHT, -UI_PAD, 102);

    lv_obj_t *kb = lv_keyboard_create(s_pw_modal);
    lv_obj_set_size(kb, UI_SCREEN_W, 250);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_pw_input);
    lv_obj_add_event_cb(kb, on_kb_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, on_kb_event, LV_EVENT_CANCEL, NULL);
}

static void on_network_row(lv_event_t *e)
{
    const size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    svc_wifi_ap_t aps[SVC_WIFI_MAX_SCAN];
    const size_t n = svc_wifi_get_scan(aps, SVC_WIFI_MAX_SCAN);
    if (index < n) {
        open_password_modal(aps[index].ssid, aps[index].secured);
    }
}

static void net_populate(void)
{
    lv_obj_clean(s_net_list);

    svc_wifi_ap_t aps[SVC_WIFI_MAX_SCAN];
    const size_t n = svc_wifi_get_scan(aps, SVC_WIFI_MAX_SCAN);

    if (n == 0) {
        lv_obj_t *lbl = lv_label_create(s_net_list);
        lv_label_set_text(lbl, i18n(STR_NO_NETWORKS));
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
        return;
    }

    for (size_t i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_create(s_net_list);
        ui_style_card_pressable(row);
        lv_obj_set_size(row, LV_PCT(100), 50);
        lv_obj_set_style_pad_hor(row, 12, LV_PART_MAIN);
        lv_obj_add_event_cb(row, on_network_row, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, aps[i].ssid);
        lv_obj_set_style_text_color(name, UI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(name, ui_font(UI_FONT_BODY), LV_PART_MAIN);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(name, UI_CONTENT_W - 90);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

        if (aps[i].secured) {
            lv_obj_t *lock = lv_label_create(row);
            lv_label_set_text(lock, LV_SYMBOL_EYE_CLOSE);
            lv_obj_set_style_text_color(lock, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
            lv_obj_set_style_text_font(lock, ui_font(UI_FONT_TINY), LV_PART_MAIN);
            lv_obj_align(lock, LV_ALIGN_RIGHT_MID, -34, 0);
        }

        lv_obj_t *bars = ui_signal_bars(row, aps[i].bars, UI_COLOR_CYAN);
        lv_obj_align(bars, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void on_net_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (!ui_nav_is_current(UI_SCR_NETWORK)) {
        return;
    }
    if (id != WATCH_EV_WIFI_SCAN_DONE && id != WATCH_EV_WIFI_STATE) {
        return;
    }
    if (bsp_display_lock(50)) {
        if (id == WATCH_EV_WIFI_SCAN_DONE) {
            lv_obj_add_flag(s_net_spinner, LV_OBJ_FLAG_HIDDEN);
            net_populate();
        }
        bsp_display_unlock();
    }
}

static void on_rescan(lv_event_t *e)
{
    (void)e;
    lv_obj_remove_flag(s_net_spinner, LV_OBJ_FLAG_HIDDEN);
    svc_wifi_scan_start();
}

static void on_net_screen_deleted(lv_event_t *e)
{
    (void)e;
    pw_close();
    s_net_list = NULL;
    s_net_spinner = NULL;
}

lv_obj_t *scr_network_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_NETWORK_SETTINGS, &body);

    lv_obj_t *head = lv_obj_create(body);
    ui_style_plain(head);
    lv_obj_set_size(head, LV_PCT(100), 26);

    lv_obj_t *cap = lv_label_create(head);
    lv_label_set_text(cap, i18n(STR_AVAILABLE_NETWORKS));
    lv_obj_set_style_text_color(cap, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(cap, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(cap, LV_ALIGN_LEFT_MID, 0, 0);

    s_net_spinner = lv_spinner_create(head);
    lv_obj_set_size(s_net_spinner, 20, 20);
    lv_obj_align(s_net_spinner, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_arc_width(s_net_spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_net_spinner, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_net_spinner, lv_color_hex(0x1B2636), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_net_spinner, UI_COLOR_ACCENT, LV_PART_INDICATOR);

    s_net_list = lv_obj_create(body);
    ui_style_plain(s_net_list);
    lv_obj_set_width(s_net_list, LV_PCT(100));
    lv_obj_set_height(s_net_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_net_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_net_list, 8, LV_PART_MAIN);

    ui_button(body, i18n(STR_SCAN), UI_COLOR_ACCENT, on_rescan, NULL);

    /* The modal lives on the top layer, so it would outlive this screen if
     * the user swiped back with the keyboard open. */
    lv_obj_add_event_cb(scr, on_net_screen_deleted, LV_EVENT_DELETE, NULL);

    net_populate();
    if (svc_wifi_status()->scanning) {
        lv_obj_remove_flag(s_net_spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_net_spinner, LV_OBJ_FLAG_HIDDEN);
        /* Opening this screen is a request to see what is around. */
        on_rescan(NULL);
    }

    ui_screen_subscribe(scr, on_net_event);
    return scr;
}

/* ======================================================== 09 bluetooth == */

static lv_obj_t *s_ble_state;
static lv_obj_t *s_ble_list;
static lv_obj_t *s_ble_spinner;

static void ble_populate(void)
{
    lv_obj_clean(s_ble_list);

    svc_ble_device_t devs[SVC_BLE_MAX_SCAN];
    const size_t n = svc_ble_get_scan(devs, SVC_BLE_MAX_SCAN);

    if (n == 0) {
        lv_obj_t *lbl = lv_label_create(s_ble_list);
        lv_label_set_text(lbl, i18n(STR_NO_DEVICES));
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
        return;
    }

    for (size_t i = 0; i < n; i++) {
        lv_obj_t *row = ui_card(s_ble_list, 52);
        lv_obj_set_style_pad_hor(row, 12, LV_PART_MAIN);

        lv_obj_t *name = lv_label_create(row);
        char addr[20];
        svc_ble_format_addr(devs[i].addr, addr, sizeof(addr));
        lv_label_set_text(name, (devs[i].name[0] != '\0') ? devs[i].name : addr);
        lv_obj_set_style_text_color(name, UI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(name, ui_font(UI_FONT_BODY), LV_PART_MAIN);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(name, UI_CONTENT_W - 110);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, -7);

        lv_obj_t *sub = lv_label_create(row);
        lv_label_set_text_fmt(sub, "%s  %d dBm", addr, (int)devs[i].rssi);
        lv_obj_set_style_text_color(sub, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
        lv_obj_set_style_text_font(sub, ui_font(UI_FONT_TINY), LV_PART_MAIN);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 0, 11);

        if (devs[i].looks_like_obd2) {
            /* Worth calling out: this is the device the OBD2 screens want. */
            lv_obj_t *tag = ui_pill(row, "OBD2", UI_COLOR_ORANGE);
            lv_obj_align(tag, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }
}

static void on_ble_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (!ui_nav_is_current(UI_SCR_BLUETOOTH)) {
        return;
    }
    if (id != WATCH_EV_BLE_SCAN_DONE && id != WATCH_EV_BLE_STATE) {
        return;
    }
    if (bsp_display_lock(50)) {
        const svc_ble_status_t *st = svc_ble_status();
        lv_label_set_text(s_ble_state,
                          (st->state == SVC_BLE_CONNECTED) ? i18n(STR_CONNECTED)
                          : (st->state == SVC_BLE_SCANNING) ? i18n(STR_SCANNING)
                          : (st->state == SVC_BLE_OFF) ? i18n(STR_OFF)
                                                       : i18n(STR_NOT_CONNECTED));
        if (id == WATCH_EV_BLE_SCAN_DONE) {
            lv_obj_add_flag(s_ble_spinner, LV_OBJ_FLAG_HIDDEN);
            ble_populate();
        }
        bsp_display_unlock();
    }
}

static void on_ble_toggle(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    svc_ble_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void on_ble_scan(lv_event_t *e)
{
    (void)e;
    lv_obj_remove_flag(s_ble_spinner, LV_OBJ_FLAG_HIDDEN);
    svc_ble_scan_start(6000);
}

lv_obj_t *scr_bluetooth_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_BLUETOOTH, &body);

    lv_obj_t *card = ui_card(body, 72);

    lv_obj_t *chip = lv_obj_create(card);
    ui_style_plain(chip);
    lv_obj_set_size(chip, 44, 44);
    lv_obj_set_style_radius(chip, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(chip, lv_color_hex(0x2563EB), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(chip, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *ico = lv_label_create(chip);
    lv_label_set_text(ico, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(ico, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_center(ico);

    s_ble_state = lv_label_create(card);
    lv_label_set_text(s_ble_state, i18n(STR_OFF));
    lv_obj_set_style_text_font(s_ble_state, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(s_ble_state, LV_ALIGN_LEFT_MID, 56, -8);

    lv_obj_t *note = lv_label_create(card);
    lv_label_set_text(note, "BLE only (no Bluetooth Classic)");
    lv_obj_set_style_text_color(note, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(note, ui_font(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_align(note, LV_ALIGN_LEFT_MID, 56, 12);

    s_ble_spinner = lv_spinner_create(card);
    lv_obj_set_size(s_ble_spinner, 20, 20);
    lv_obj_align(s_ble_spinner, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_arc_width(s_ble_spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ble_spinner, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ble_spinner, lv_color_hex(0x1B2636), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ble_spinner, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_flag(s_ble_spinner, LV_OBJ_FLAG_HIDDEN);

    ui_toggle_row(body, i18n(STR_BLUETOOTH), svc_ble_status()->state != SVC_BLE_OFF,
                  on_ble_toggle, NULL);

    lv_obj_t *cap = lv_label_create(body);
    lv_label_set_text(cap, i18n(STR_DEVICES));
    lv_obj_set_style_text_color(cap, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(cap, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);

    s_ble_list = lv_obj_create(body);
    ui_style_plain(s_ble_list);
    lv_obj_set_width(s_ble_list, LV_PCT(100));
    lv_obj_set_height(s_ble_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_ble_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ble_list, 8, LV_PART_MAIN);

    ui_button(body, i18n(STR_SCAN), UI_COLOR_ACCENT, on_ble_scan, NULL);

    ble_populate();
    ui_screen_subscribe(scr, on_ble_event);
    return scr;
}
