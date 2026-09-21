/*
 * The handful of compound widgets every screen is built from.
 *
 * Keeping them here is what makes 27 screens look like one product: the
 * header, the cards and the rows are defined once, so a change to the
 * card radius or the row height lands everywhere at the same time.
 */
#pragma once

#include "lvgl.h"
#include "watch_ui/ui_theme.h"
#include "watch_svc/svc_i18n.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The standard screen: black background, header, scrollable body.
 *
 * @param title_id     Localised title for the header.
 * @param[out] out_body Receives the content container, already padded and
 *                      laid out as a vertical flex column.
 * @return The screen object, ready to hand back from a create function.
 */
lv_obj_t *ui_screen_base(i18n_id_t title_id, lv_obj_t **out_body);

/**
 * @brief Same, but with a literal title for screens whose name is data
 *        (a network SSID, a trouble code).
 */
lv_obj_t *ui_screen_base_text(const char *title, lv_obj_t **out_body);

/**
 * @brief A header bar: back chevron, title, clock.
 *
 * Also wires the swipe-right-to-go-back gesture onto @p parent's screen.
 */
lv_obj_t *ui_widgets_header(lv_obj_t *parent, const char *title);

/** @brief A plain card container. */
lv_obj_t *ui_card(lv_obj_t *parent, lv_coord_t height);

/**
 * @brief A card with a coloured icon, a caption and a big value.
 *
 * @param[out] out_value Receives the value label so the screen can update
 *                       it without walking the tree.
 */
lv_obj_t *ui_stat_tile(lv_obj_t *parent, const char *icon, lv_color_t icon_color,
                       const char *caption, const char *value, lv_obj_t **out_value);

/**
 * @brief A "label .... value" row, as used on every detail screen.
 *
 * @param[out] out_value Receives the value label.
 */
lv_obj_t *ui_kv_row(lv_obj_t *parent, const char *key, const char *value,
                    lv_obj_t **out_value);

/**
 * @brief A tappable settings row: icon, label, chevron.
 *
 * @param cb Click handler. May be NULL for a row that only displays.
 */
lv_obj_t *ui_menu_row(lv_obj_t *parent, const char *icon, lv_color_t icon_color,
                      const char *label, lv_event_cb_t cb, void *user_data);

/** @brief A settings row carrying a switch instead of a chevron. */
lv_obj_t *ui_toggle_row(lv_obj_t *parent, const char *label, bool state,
                        lv_event_cb_t cb, void *user_data);

/** @brief A coloured status pill, as on the Vehicle Status screen. */
lv_obj_t *ui_pill(lv_obj_t *parent, const char *text, lv_color_t color);

/** @brief A full-width filled button. */
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_color_t color,
                    lv_event_cb_t cb, void *user_data);

/**
 * @brief A centred message for an empty or unavailable state.
 *
 * Used wherever hardware is missing - no SD card, no camera, no adapter -
 * so those cases look designed rather than broken.
 */
lv_obj_t *ui_empty_state(lv_obj_t *parent, const char *icon, const char *message,
                         const char *hint);

/** @brief A ring gauge with a value label in the middle. */
lv_obj_t *ui_ring(lv_obj_t *parent, lv_coord_t size, lv_color_t color,
                  int32_t value, const char *centre_text, lv_obj_t **out_label);

/** @brief A 0-4 bar signal indicator drawn from four small rectangles. */
lv_obj_t *ui_signal_bars(lv_obj_t *parent, uint8_t bars, lv_color_t color);

/** @brief Update an existing indicator built by ui_signal_bars(). */
void ui_signal_bars_set(lv_obj_t *obj, uint8_t bars, lv_color_t color);

#ifdef __cplusplus
}
#endif
