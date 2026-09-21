/*
 * 01 - Boot / splash.
 *
 * Shown while the services finish coming up. ui_start() moves on from here
 * after a short hold, so nothing on this screen needs to be interactive.
 */
#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_ota.h"

lv_obj_t *scr_boot_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_style_plain(scr);
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    /* Mark: two offset rounded bars in the accent colours, echoing the
     * logo in the design set without needing an image asset. */
    lv_obj_t *mark = lv_obj_create(scr);
    ui_style_plain(mark);
    lv_obj_set_size(mark, 96, 96);
    lv_obj_align(mark, LV_ALIGN_CENTER, 0, -90);

    lv_obj_t *bar1 = lv_obj_create(mark);
    ui_style_plain(bar1);
    lv_obj_set_size(bar1, 76, 22);
    lv_obj_set_style_radius(bar1, 11, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar1, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar1, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(bar1, LV_ALIGN_CENTER, -8, -16);

    lv_obj_t *bar2 = lv_obj_create(mark);
    ui_style_plain(bar2);
    lv_obj_set_size(bar2, 76, 22);
    lv_obj_set_style_radius(bar2, 11, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar2, UI_COLOR_CYAN, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar2, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(bar2, LV_ALIGN_CENTER, 8, 16);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32-S3");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(title, ui_font(UI_FONT_LARGE), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Touch AMOLED 2.06");
    lv_obj_set_style_text_color(sub, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(sub, ui_font(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 34);

    lv_obj_t *spinner = lv_spinner_create(scr);
    lv_obj_set_size(spinner, 46, 46);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 110);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x1B2636), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_INDICATOR);

    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, i18n(STR_LOADING));
    lv_obj_set_style_text_color(status, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(status, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 152);

    lv_obj_t *ver = lv_label_create(scr);
    lv_label_set_text_fmt(ver, "v%s", svc_ota_running_version());
    lv_obj_set_style_text_color(ver, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(ver, ui_font(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -16);

    return scr;
}
