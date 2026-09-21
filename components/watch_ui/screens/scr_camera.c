/*
 * 12 - Camera, 13 - AI Vision, 14 - Detection details.
 *
 * All three need the add-on module. This board has no camera of its own,
 * so when nothing is attached the screens say so plainly instead of showing
 * a frozen or invented frame.
 *
 * The preview is blitted into an lv_canvas from the service's RGB565 buffer.
 * Vision draws its boxes as ordinary LVGL objects on top of that canvas
 * rather than into the pixels, so a new frame never has to erase them.
 */
#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_camera.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_storage.h"
#include "watch_svc/svc_vision.h"

#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"

/* The preview is 320x240; the panel is 410 wide, so it sits in a 320-wide
 * frame with the controls below. */
#define PREVIEW_W SVC_CAMERA_PREVIEW_W
#define PREVIEW_H SVC_CAMERA_PREVIEW_H

static lv_obj_t  *s_canvas;
static uint16_t  *s_canvas_buf;
static lv_obj_t  *s_fps_label;
static lv_obj_t  *s_count_label;
static lv_obj_t  *s_box[SVC_VISION_MAX_OBJECTS];
static lv_obj_t  *s_box_label[SVC_VISION_MAX_OBJECTS];

static void release_canvas(lv_event_t *e)
{
    (void)e;
    /* The canvas draw buffer is ours, not LVGL's, so it has to be freed
     * with the screen. */
    if (s_canvas_buf != NULL) {
        heap_caps_free(s_canvas_buf);
        s_canvas_buf = NULL;
    }
    s_canvas = NULL;
    /* Stop the hardware as soon as the screen goes: the SPI reads are the
     * heaviest thing the watch does. */
    svc_vision_set_running(false);
    svc_camera_preview(false);
}

static lv_obj_t *make_canvas(lv_obj_t *parent)
{
    s_canvas_buf = heap_caps_malloc(PREVIEW_W * PREVIEW_H * 2,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_canvas_buf == NULL) {
        return NULL;
    }
    memset(s_canvas_buf, 0, PREVIEW_W * PREVIEW_H * 2);

    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, s_canvas_buf, PREVIEW_W, PREVIEW_H,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(canvas, PREVIEW_W, PREVIEW_H);
    return canvas;
}

static void blit_frame(void)
{
    if (s_canvas == NULL || s_canvas_buf == NULL) {
        return;
    }
    const uint16_t *src = svc_camera_lock(30);
    if (src == NULL) {
        return;
    }
    memcpy(s_canvas_buf, src, PREVIEW_W * PREVIEW_H * 2);
    svc_camera_unlock();
    lv_obj_invalidate(s_canvas);
}

/* ============================================================ 12 camera == */

static void on_capture(lv_event_t *e)
{
    (void)e;
    char path[96];
    if (svc_camera_capture_still(path, sizeof(path)) == ESP_OK) {
        const char *base = strrchr(path, '/');
        ui_toast((base != NULL) ? base + 1 : path, UI_COLOR_GREEN);
    } else {
        ui_toast(i18n(STR_ERROR), UI_COLOR_RED);
    }
}

static void on_go_vision(lv_event_t *e)
{
    (void)e;
    ui_nav_go(UI_SCR_VISION);
}

static void camera_tick(lv_timer_t *t)
{
    (void)t;
    blit_frame();
    const svc_camera_status_t *st = svc_camera_status();
    lv_label_set_text_fmt(s_fps_label, "FPS %u", (unsigned)st->fps);
}

lv_obj_t *scr_camera_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_CAMERA, &body);

    if (!svc_camera_available()) {
        ui_empty_state(body, LV_SYMBOL_IMAGE, i18n(STR_NO_CAMERA), i18n(STR_CAMERA_HINT));
        return scr;
    }

    lv_obj_t *frame = ui_card(body, PREVIEW_H + 16);
    lv_obj_set_style_pad_all(frame, 4, LV_PART_MAIN);

    s_canvas = make_canvas(frame);
    if (s_canvas == NULL) {
        ui_empty_state(body, LV_SYMBOL_WARNING, i18n(STR_ERROR), "out of memory");
        return scr;
    }
    lv_obj_center(s_canvas);

    s_fps_label = lv_label_create(frame);
    lv_label_set_text(s_fps_label, "FPS 0");
    lv_obj_set_style_text_color(s_fps_label, UI_COLOR_GREEN, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_fps_label, ui_font(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_align(s_fps_label, LV_ALIGN_TOP_RIGHT, -6, 6);

    /* ---- controls ---- */
    lv_obj_t *controls = lv_obj_create(body);
    ui_style_plain(controls);
    lv_obj_set_size(controls, LV_PCT(100), 76);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *shutter = lv_button_create(controls);
    lv_obj_set_size(shutter, 64, 64);
    lv_obj_set_style_radius(shutter, 32, LV_PART_MAIN);
    lv_obj_set_style_bg_color(shutter, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(shutter, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(shutter, lv_color_hex(0x123057), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(shutter, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(shutter, on_capture, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sh_ico = lv_label_create(shutter);
    lv_label_set_text(sh_ico, LV_SYMBOL_IMAGE);
    lv_obj_center(sh_ico);

    lv_obj_t *ai = lv_button_create(controls);
    lv_obj_set_size(ai, 52, 52);
    lv_obj_set_style_radius(ai, 26, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ai, lv_color_hex(0x1B2636), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ai, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(ai, on_go_vision, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ai_ico = lv_label_create(ai);
    lv_label_set_text(ai_ico, LV_SYMBOL_EYE_OPEN);
    lv_obj_center(ai_ico);

    lv_obj_t *info = ui_card(body, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    const svc_camera_status_t *st = svc_camera_status();
    ui_kv_row(info, i18n(STR_CAMERA), st->model, NULL);
    ui_kv_row(info, i18n(STR_STORAGE), svc_storage_media_root(), NULL);

    lv_obj_add_event_cb(scr, release_canvas, LV_EVENT_DELETE, NULL);
    svc_camera_preview(true);
    ui_screen_add_timer(scr, camera_tick, 100);
    return scr;
}

/* ========================================================= 13 ai vision == */

static void clear_boxes(void)
{
    for (int i = 0; i < SVC_VISION_MAX_OBJECTS; i++) {
        if (s_box[i] != NULL) {
            lv_obj_add_flag(s_box[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static lv_color_t class_color(svc_vision_class_t cls)
{
    switch (cls) {
    case SVC_VISION_CLASS_PERSON: return UI_COLOR_GREEN;
    case SVC_VISION_CLASS_CAR:    return UI_COLOR_ACCENT;
    default:                      return UI_COLOR_CYAN;
    }
}

static void draw_detections(void)
{
    const svc_vision_result_t *r = svc_vision_latest();
    clear_boxes();

    for (uint8_t i = 0; i < r->count && i < SVC_VISION_MAX_OBJECTS; i++) {
        const svc_vision_object_t *o = &r->objects[i];
        if (s_box[i] == NULL) {
            continue;
        }
        const lv_color_t c = class_color(o->cls);

        lv_obj_set_pos(s_box[i], o->x, o->y);
        lv_obj_set_size(s_box[i], (o->w > 4) ? o->w : 4, (o->h > 4) ? o->h : 4);
        lv_obj_set_style_border_color(s_box[i], c, LV_PART_MAIN);
        lv_obj_remove_flag(s_box[i], LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text_fmt(s_box_label[i], "%s %u%%", o->label,
                              (unsigned)o->confidence);
        lv_obj_set_style_bg_color(s_box_label[i], c, LV_PART_MAIN);
    }

    lv_label_set_text_fmt(s_fps_label, "FPS %u", (unsigned)r->fps);
    lv_label_set_text_fmt(s_count_label, "%s: %u", i18n(STR_OBJECTS),
                          (unsigned)r->count);
}

static void vision_tick(lv_timer_t *t)
{
    (void)t;
    blit_frame();
    draw_detections();
}

static void on_go_detail(lv_event_t *e)
{
    (void)e;
    ui_nav_go(UI_SCR_DETECTION);
}

lv_obj_t *scr_vision_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_AI_VISION, &body);

    if (!svc_camera_available()) {
        ui_empty_state(body, LV_SYMBOL_EYE_OPEN, i18n(STR_NO_CAMERA),
                       i18n(STR_CAMERA_HINT));
        return scr;
    }

    lv_obj_t *frame = ui_card(body, PREVIEW_H + 16);
    lv_obj_set_style_pad_all(frame, 4, LV_PART_MAIN);

    s_canvas = make_canvas(frame);
    if (s_canvas == NULL) {
        ui_empty_state(body, LV_SYMBOL_WARNING, i18n(STR_ERROR), "out of memory");
        return scr;
    }
    lv_obj_center(s_canvas);

    /* Bounding boxes are pre-created and hidden, so a busy frame never
     * allocates during the refresh. */
    for (int i = 0; i < SVC_VISION_MAX_OBJECTS; i++) {
        s_box[i] = lv_obj_create(s_canvas);
        ui_style_plain(s_box[i]);
        lv_obj_set_style_border_width(s_box[i], 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_box[i], UI_COLOR_GREEN, LV_PART_MAIN);
        lv_obj_set_style_radius(s_box[i], 3, LV_PART_MAIN);
        lv_obj_add_flag(s_box[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_box[i], LV_OBJ_FLAG_CLICKABLE);

        s_box_label[i] = lv_label_create(s_box[i]);
        lv_label_set_text(s_box_label[i], "");
        lv_obj_set_style_text_color(s_box_label[i], lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_box_label[i], ui_font(UI_FONT_TINY), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_box_label[i], UI_COLOR_GREEN, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_box_label[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(s_box_label[i], 3, LV_PART_MAIN);
        lv_obj_align(s_box_label[i], LV_ALIGN_OUT_TOP_LEFT, 0, 0);
    }

    s_fps_label = lv_label_create(frame);
    lv_label_set_text(s_fps_label, "FPS 0");
    lv_obj_set_style_text_color(s_fps_label, UI_COLOR_GREEN, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_fps_label, ui_font(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_align(s_fps_label, LV_ALIGN_TOP_RIGHT, -6, 6);

    /* ---- summary ---- */
    lv_obj_t *summary = ui_card(body, 52);
    ui_style_card_pressable(summary);
    lv_obj_add_event_cb(summary, on_go_detail, LV_EVENT_CLICKED, NULL);

    s_count_label = lv_label_create(summary);
    lv_label_set_text_fmt(s_count_label, "%s: 0", i18n(STR_OBJECTS));
    lv_obj_set_style_text_color(s_count_label, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_count_label, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(s_count_label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *chev = lv_label_create(summary);
    lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chev, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *backend = lv_label_create(body);
    lv_label_set_text_fmt(backend, "%s: %s", i18n(STR_AI_VISION),
                          svc_vision_backend_name());
    lv_obj_set_style_text_color(backend, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
    lv_obj_set_style_text_font(backend, ui_font(UI_FONT_TINY), LV_PART_MAIN);

    lv_obj_add_event_cb(scr, release_canvas, LV_EVENT_DELETE, NULL);
    svc_camera_preview(true);
    svc_vision_set_running(true);
    ui_screen_add_timer(scr, vision_tick, 100);
    return scr;
}

/* ==================================================== 14 detection detail */

static lv_obj_t *s_detail_list;
static lv_obj_t *s_detail_summary;

static void detail_fill(void)
{
    lv_obj_clean(s_detail_list);
    const svc_vision_result_t *r = svc_vision_latest();

    if (r->count == 0) {
        lv_obj_t *lbl = lv_label_create(s_detail_list);
        lv_label_set_text(lbl, i18n(STR_NO_DETECTIONS));
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    }

    for (uint8_t i = 0; i < r->count && i < SVC_VISION_MAX_OBJECTS; i++) {
        const svc_vision_object_t *o = &r->objects[i];
        const lv_color_t c = class_color(o->cls);

        lv_obj_t *row = ui_card(s_detail_list, 50);
        lv_obj_set_style_pad_hor(row, 12, LV_PART_MAIN);

        lv_obj_t *chip = lv_obj_create(row);
        ui_style_plain(chip);
        lv_obj_set_size(chip, 28, 28);
        lv_obj_set_style_radius(chip, 9, LV_PART_MAIN);
        lv_obj_set_style_bg_color(chip, c, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_align(chip, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *n = lv_label_create(chip);
        lv_label_set_text_fmt(n, "%u", (unsigned)(i + 1));
        lv_obj_set_style_text_color(n, UI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(n, ui_font(UI_FONT_TINY), LV_PART_MAIN);
        lv_obj_center(n);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, o->label);
        lv_obj_set_style_text_color(name, UI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(name, ui_font_text(UI_FONT_BODY), LV_PART_MAIN);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 38, -7);

        lv_obj_t *box = lv_label_create(row);
        lv_label_set_text_fmt(box, "%d,%d  %dx%d", (int)o->x, (int)o->y,
                              (int)o->w, (int)o->h);
        lv_obj_set_style_text_color(box, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
        lv_obj_set_style_text_font(box, ui_font(UI_FONT_TINY), LV_PART_MAIN);
        lv_obj_align(box, LV_ALIGN_LEFT_MID, 38, 11);

        lv_obj_t *conf = lv_label_create(row);
        lv_label_set_text_fmt(conf, "%u%%", (unsigned)o->confidence);
        lv_obj_set_style_text_color(conf, c, LV_PART_MAIN);
        lv_obj_set_style_text_font(conf, ui_font(UI_FONT_BODY), LV_PART_MAIN);
        lv_obj_align(conf, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    lv_label_set_text_fmt(s_detail_summary, "FPS %u   %s %lu ms",
                          (unsigned)r->fps, i18n(STR_AI_VISION),
                          (unsigned long)r->infer_ms);
}

static void detail_tick(lv_timer_t *t)
{
    (void)t;
    detail_fill();
}

lv_obj_t *scr_detection_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_DETECTION_DETAIL, &body);

    s_detail_summary = lv_label_create(body);
    lv_label_set_text(s_detail_summary, "");
    lv_obj_set_style_text_color(s_detail_summary, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_detail_summary, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);

    s_detail_list = lv_obj_create(body);
    ui_style_plain(s_detail_list);
    lv_obj_set_width(s_detail_list, LV_PCT(100));
    lv_obj_set_height(s_detail_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_detail_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_detail_list, 8, LV_PART_MAIN);

    detail_fill();
    /* Slower than the preview: this is a readable list, not a viewfinder. */
    ui_screen_add_timer(scr, detail_tick, 500);
    return scr;
}
