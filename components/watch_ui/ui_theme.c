#include "watch_ui/ui_theme.h"

#include "esp_log.h"

static const char *TAG = "ui_theme";

static bool s_cjk;

void ui_theme_set_lang(watch_lang_t lang)
{
    s_cjk = (lang == WATCH_LANG_JP);
}

const lv_font_t *ui_font(ui_font_role_t role)
{
    /* Numerals and Latin: always Montserrat, at whatever size the role
     * calls for. */
    switch (role) {
    case UI_FONT_HUGE:  return &lv_font_montserrat_48;
    case UI_FONT_LARGE: return &lv_font_montserrat_36;
    case UI_FONT_TITLE: return &lv_font_montserrat_20;
    case UI_FONT_BODY:  return &lv_font_montserrat_16;
    case UI_FONT_SMALL: return &lv_font_montserrat_14;
    case UI_FONT_TINY:  return &lv_font_montserrat_12;
    }
    return &lv_font_montserrat_16;
}

const lv_font_t *ui_font_text(ui_font_role_t role)
{
    if (!s_cjk) {
        return ui_font(role);
    }
    /* The bundled Source Han Sans subset exists only at 16 px. Rather than
     * scale a bitmap font and get mush, translated text uses it at its
     * native size for the body-ish roles and Montserrat stays on the roles
     * that only ever carry digits. */
    switch (role) {
    case UI_FONT_HUGE:
    case UI_FONT_LARGE:
        return ui_font(role);          /* numeric only by design */
    case UI_FONT_TITLE:
    case UI_FONT_BODY:
    case UI_FONT_SMALL:
    case UI_FONT_TINY:
    default:
        return &lv_font_source_han_sans_sc_16_cjk;
    }
}

void ui_theme_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    /* LVGL's default theme draws light-on-grey; the watch overrides colour
     * per object, so all that is needed here is a sane default font. */
    lv_display_t *disp = lv_display_get_default();
    if (disp != NULL) {
        lv_theme_t *theme = lv_theme_default_init(disp, UI_COLOR_ACCENT, UI_COLOR_CYAN,
                                                  true /* dark */,
                                                  ui_font_text(UI_FONT_BODY));
        lv_display_set_theme(disp, theme);
    }
    ESP_LOGI(TAG, "theme ready");
}

void ui_style_plain(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_style_card(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, UI_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, UI_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 12, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_style_card_pressable(lv_obj_t *obj)
{
    ui_style_card(obj);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    /* A slight lift on press is enough feedback on a screen this size. */
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x1B2636), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_PRESSED);
}

void ui_style_button(lv_obj_t *obj, lv_color_t color)
{
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, UI_RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
}
