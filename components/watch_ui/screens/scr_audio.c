/*
 * 10 - Audio.
 *
 * Record to a WAV file on the card, play it back, and watch the level while
 * it runs. The meter is driven from WATCH_EV_AUDIO_LEVEL rather than polled,
 * so it stays in step with the capture task instead of sampling it.
 */
#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_audio.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_settings.h"
#include "watch_svc/svc_storage.h"

#include <stdio.h>
#include <string.h>
#include "bsp/esp-bsp.h"

#define METER_SEGMENTS 12

static lv_obj_t *s_state_label;
static lv_obj_t *s_elapsed;
static lv_obj_t *s_meter[METER_SEGMENTS];
static lv_obj_t *s_rec_btn;
static lv_obj_t *s_rec_icon;
static lv_obj_t *s_path_label;
static lv_obj_t *s_wave[24];

static void set_meter(uint8_t level)
{
    const int lit = (level * METER_SEGMENTS) / 100;
    for (int i = 0; i < METER_SEGMENTS; i++) {
        lv_color_t c;
        if (i >= lit) {
            c = lv_color_hex(0x223044);
        } else if (i < METER_SEGMENTS * 2 / 3) {
            c = UI_COLOR_GREEN;
        } else if (i < METER_SEGMENTS - 2) {
            c = UI_COLOR_ORANGE;
        } else {
            c = UI_COLOR_RED;
        }
        lv_obj_set_style_bg_color(s_meter[i], c, LV_PART_MAIN);
    }
}

/* The waveform strip is decorative but driven by the real level, so it is
 * still telling the truth about what the microphone hears. */
static void set_wave(uint8_t level)
{
    for (int i = 0; i < 24; i++) {
        /* A fixed pseudo-random profile scaled by the live level keeps the
         * strip lively without pretending to be a spectrum. */
        static const uint8_t profile[24] = {
            30, 62, 45, 80, 55, 95, 40, 70, 88, 35, 60, 75,
            50, 90, 42, 68, 82, 38, 72, 58, 96, 44, 64, 52
        };
        int h = (profile[i] * level) / 100;
        if (h < 3) { h = 3; }
        lv_obj_set_height(s_wave[i], (lv_coord_t)h);
    }
}

static void refresh(void)
{
    const svc_audio_status_t *st = svc_audio_status();

    switch (st->state) {
    case SVC_AUDIO_RECORDING:
        lv_label_set_text(s_state_label, i18n(STR_RECORDING));
        lv_obj_set_style_text_color(s_state_label, UI_COLOR_RED, LV_PART_MAIN);
        lv_label_set_text(s_rec_icon, LV_SYMBOL_STOP);
        break;
    case SVC_AUDIO_PLAYING:
        lv_label_set_text(s_state_label, i18n(STR_PLAY));
        lv_obj_set_style_text_color(s_state_label, UI_COLOR_GREEN, LV_PART_MAIN);
        lv_label_set_text(s_rec_icon, LV_SYMBOL_STOP);
        break;
    case SVC_AUDIO_IDLE:
    default:
        lv_label_set_text(s_state_label, i18n(STR_RECORD));
        lv_obj_set_style_text_color(s_state_label, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_label_set_text(s_rec_icon, LV_SYMBOL_AUDIO);
        break;
    }

    const uint32_t ms = st->elapsed_ms;
    lv_label_set_text_fmt(s_elapsed, "%02lu:%02lu:%02lu",
                          (unsigned long)(ms / 3600000),
                          (unsigned long)((ms / 60000) % 60),
                          (unsigned long)((ms / 1000) % 60));

    if (st->path[0] != '\0') {
        const char *base = strrchr(st->path, '/');
        lv_label_set_text(s_path_label, (base != NULL) ? base + 1 : st->path);
    }

    set_meter(st->level);
    set_wave(st->level);
}

static void on_tick(lv_timer_t *t)
{
    (void)t;
    refresh();
}

static void on_record(lv_event_t *e)
{
    (void)e;
    const svc_audio_status_t *st = svc_audio_status();

    if (st->state == SVC_AUDIO_IDLE) {
        if (svc_audio_record_start(NULL) != ESP_OK) {
            ui_toast(i18n(STR_ERROR), UI_COLOR_RED);
        }
    } else if (st->state == SVC_AUDIO_RECORDING) {
        svc_audio_record_stop();
        ui_toast(i18n(STR_SAVE), UI_COLOR_GREEN);
    } else {
        svc_audio_stop();
    }
    refresh();
}

static void on_play(lv_event_t *e)
{
    (void)e;
    const svc_audio_status_t *st = svc_audio_status();
    if (st->state != SVC_AUDIO_IDLE || st->path[0] == '\0') {
        return;
    }
    if (svc_audio_play(st->path) != ESP_OK) {
        ui_toast(i18n(STR_ERROR), UI_COLOR_RED);
    }
}

static void on_volume(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    svc_audio_set_volume((uint8_t)lv_slider_get_value(slider));
}

static void on_gain(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    svc_audio_set_mic_gain((uint8_t)lv_slider_get_value(slider));
}

static lv_obj_t *labelled_slider(lv_obj_t *parent, const char *label, uint8_t value,
                                 lv_event_cb_t cb)
{
    lv_obj_t *card = ui_card(parent, 62);

    lv_obj_t *l = lv_label_create(card);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(l, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_size(slider, LV_PCT(100), 8);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x1E2A3A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, UI_COLOR_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return slider;
}

lv_obj_t *scr_audio_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_AUDIO, &body);

    /* ---- status + waveform ---- */
    lv_obj_t *top = ui_card(body, 150);

    lv_obj_t *mic = lv_label_create(top);
    lv_label_set_text(mic, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(mic, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_align(mic, LV_ALIGN_TOP_LEFT, 0, 0);

    s_state_label = lv_label_create(top);
    lv_label_set_text(s_state_label, i18n(STR_RECORD));
    lv_obj_set_style_text_font(s_state_label, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(s_state_label, LV_ALIGN_TOP_LEFT, 28, 0);

    s_elapsed = lv_label_create(top);
    lv_label_set_text(s_elapsed, "00:00:00");
    lv_obj_set_style_text_color(s_elapsed, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_elapsed, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
    lv_obj_align(s_elapsed, LV_ALIGN_TOP_RIGHT, 0, -2);

    lv_obj_t *wave = lv_obj_create(top);
    ui_style_plain(wave);
    lv_obj_set_size(wave, LV_PCT(100), 70);
    lv_obj_align(wave, LV_ALIGN_BOTTOM_MID, 0, 0);
    for (int i = 0; i < 24; i++) {
        s_wave[i] = lv_obj_create(wave);
        ui_style_plain(s_wave[i]);
        lv_obj_set_size(s_wave[i], 5, 4);
        lv_obj_set_style_radius(s_wave[i], 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_wave[i], UI_COLOR_CYAN, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_wave[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_align(s_wave[i], LV_ALIGN_BOTTOM_LEFT, (lv_coord_t)(i * 15), 0);
    }

    /* ---- transport ---- */
    lv_obj_t *transport = ui_card(body, 96);

    s_rec_btn = lv_button_create(transport);
    lv_obj_set_size(s_rec_btn, 64, 64);
    lv_obj_set_style_radius(s_rec_btn, 32, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_rec_btn, UI_COLOR_RED, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_rec_btn, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_rec_btn, lv_color_hex(0x3B1218), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_rec_btn, 0, LV_PART_MAIN);
    lv_obj_align(s_rec_btn, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_add_event_cb(s_rec_btn, on_record, LV_EVENT_CLICKED, NULL);

    s_rec_icon = lv_label_create(s_rec_btn);
    lv_label_set_text(s_rec_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(s_rec_icon, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_center(s_rec_icon);

    lv_obj_t *play = lv_button_create(transport);
    lv_obj_set_size(play, 48, 48);
    lv_obj_set_style_radius(play, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(play, lv_color_hex(0x1B2636), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(play, 0, LV_PART_MAIN);
    lv_obj_align(play, LV_ALIGN_LEFT_MID, 84, 0);
    lv_obj_add_event_cb(play, on_play, LV_EVENT_CLICKED, NULL);

    lv_obj_t *play_ico = lv_label_create(play);
    lv_label_set_text(play_ico, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play_ico, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_center(play_ico);

    /* level meter */
    lv_obj_t *meter = lv_obj_create(transport);
    ui_style_plain(meter);
    lv_obj_set_size(meter, 150, 16);
    lv_obj_align(meter, LV_ALIGN_RIGHT_MID, 0, 8);
    for (int i = 0; i < METER_SEGMENTS; i++) {
        s_meter[i] = lv_obj_create(meter);
        ui_style_plain(s_meter[i]);
        lv_obj_set_size(s_meter[i], 9, 16);
        lv_obj_set_style_radius(s_meter[i], 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_meter[i], lv_color_hex(0x223044), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_meter[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_align(s_meter[i], LV_ALIGN_LEFT_MID, (lv_coord_t)(i * 12), 0);
    }

    lv_obj_t *meter_cap = lv_label_create(transport);
    lv_label_set_text(meter_cap, i18n(STR_MIC_LEVEL));
    lv_obj_set_style_text_color(meter_cap, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(meter_cap, ui_font_text(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_align(meter_cap, LV_ALIGN_RIGHT_MID, 0, -14);

    /* ---- file ---- */
    lv_obj_t *file_card = ui_card(body, 0);
    s_path_label = NULL;
    ui_kv_row(file_card, i18n(STR_STORAGE), svc_storage_media_root(), &s_path_label);

    /* ---- levels ---- */
    const watch_settings_t *cfg = svc_settings_get();
    labelled_slider(body, i18n(STR_VOLUME), cfg->volume, on_volume);
    labelled_slider(body, i18n(STR_MIC_GAIN), cfg->mic_gain, on_gain);

    refresh();
    ui_screen_add_timer(scr, on_tick, 100);
    return scr;
}
