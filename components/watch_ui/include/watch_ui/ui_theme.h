/*
 * Visual language for the watch.
 *
 * Built for an AMOLED panel, which is why the background is true black
 * rather than a dark grey: an unlit pixel draws no current, so the black
 * areas of every screen are free. Cards sit just above black so they read
 * as surfaces without lighting up the whole panel.
 */
#pragma once

#include "lvgl.h"
#include "watch_svc/svc_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ colours */

#define UI_COLOR_BG          lv_color_hex(0x000000)  /* unlit - costs nothing */
#define UI_COLOR_CARD        lv_color_hex(0x121A27)
#define UI_COLOR_CARD_ALT    lv_color_hex(0x0C121C)
#define UI_COLOR_BORDER      lv_color_hex(0x1F2C3E)
#define UI_COLOR_HEADER      lv_color_hex(0x070B12)

#define UI_COLOR_TEXT        lv_color_hex(0xFFFFFF)
#define UI_COLOR_TEXT_DIM    lv_color_hex(0x94A3B8)
#define UI_COLOR_TEXT_FAINT  lv_color_hex(0x5A6B82)

#define UI_COLOR_ACCENT      lv_color_hex(0x3B82F6)  /* primary blue */
#define UI_COLOR_CYAN        lv_color_hex(0x22D3EE)
#define UI_COLOR_GREEN       lv_color_hex(0x22C55E)
#define UI_COLOR_ORANGE      lv_color_hex(0xF59E0B)
#define UI_COLOR_RED         lv_color_hex(0xEF4444)
#define UI_COLOR_PURPLE      lv_color_hex(0xA855F7)
#define UI_COLOR_PINK        lv_color_hex(0xEC4899)

/* ------------------------------------------------------------------ metrics */

#define UI_SCREEN_W          410
#define UI_SCREEN_H          502

#define UI_PAD               14   /* gutter from the panel edge */
#define UI_GAP               10   /* between cards */
#define UI_RADIUS            16
#define UI_RADIUS_SM         10
#define UI_HEADER_H          44

#define UI_CONTENT_W         (UI_SCREEN_W - 2 * UI_PAD)
#define UI_CONTENT_H         (UI_SCREEN_H - UI_HEADER_H)

/* Screen transitions. Long enough to read as motion, short enough not to
 * feel like waiting. */
#define UI_ANIM_MS           220

/* ------------------------------------------------------------------- fonts */

typedef enum {
    UI_FONT_HUGE,     /* the clock on the watch face */
    UI_FONT_LARGE,    /* headline numbers on cards */
    UI_FONT_TITLE,    /* screen titles */
    UI_FONT_BODY,     /* list rows, values */
    UI_FONT_SMALL,    /* units, captions */
    UI_FONT_TINY,     /* axis labels */
} ui_font_role_t;

/**
 * @brief The font for @p role in the current language.
 *
 * Japanese text falls back to the bundled CJK face, which only exists at
 * 16 px - so the large roles return Montserrat regardless of language and
 * screens must keep big type numeric. Numerals and Latin always come from
 * Montserrat, which is why the clock looks identical in both languages.
 */
const lv_font_t *ui_font(ui_font_role_t role);

/** @brief The font to use for a translated label at @p role. */
const lv_font_t *ui_font_text(ui_font_role_t role);

/** @brief Re-resolve the fonts after a language change. */
void ui_theme_set_lang(watch_lang_t lang);

/** @brief Install the theme. Call once, under the LVGL lock. */
void ui_theme_init(void);

/* ------------------------------------------------------------------ styles */

/** @brief A rounded dark card. */
void ui_style_card(lv_obj_t *obj);

/** @brief A card that reads as pressable. */
void ui_style_card_pressable(lv_obj_t *obj);

/** @brief Strip an object's default padding, border and scrolling. */
void ui_style_plain(lv_obj_t *obj);

/** @brief A filled accent button. */
void ui_style_button(lv_obj_t *obj, lv_color_t color);

#ifdef __cplusplus
}
#endif
