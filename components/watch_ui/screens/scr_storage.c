/*
 * 11 - SD card.
 *
 * Usage breakdown plus a file browser. The card is hot-pluggable, so the
 * screen listens for WATCH_EV_SD_STATE and rebuilds rather than assuming
 * whatever was true when it opened.
 */
#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_storage.h"

#include <stdio.h>
#include <string.h>
#include "bsp/esp-bsp.h"

static lv_obj_t *s_ring;
static lv_obj_t *s_ring_label;
static lv_obj_t *s_used_label;
static lv_obj_t *s_img_label;
static lv_obj_t *s_vid_label;
static lv_obj_t *s_oth_label;
static lv_obj_t *s_browser;
static char      s_cwd[192];

static void refresh_usage(void)
{
    const svc_storage_info_t *info = svc_storage_sd_info();

    char used[16], total[16];
    svc_storage_format_size(info->used_bytes, used, sizeof(used));
    svc_storage_format_size(info->total_bytes, total, sizeof(total));

    const int pct = (info->total_bytes > 0)
                    ? (int)((info->used_bytes * 100) / info->total_bytes) : 0;
    lv_arc_set_value(s_ring, pct);
    lv_label_set_text_fmt(s_ring_label, "%d%%", pct);
    lv_label_set_text_fmt(s_used_label, "%s / %s", used, total);

    char buf[16];
    svc_storage_format_size(info->images_bytes, buf, sizeof(buf));
    lv_label_set_text(s_img_label, buf);
    svc_storage_format_size(info->videos_bytes, buf, sizeof(buf));
    lv_label_set_text(s_vid_label, buf);
    svc_storage_format_size(info->other_bytes, buf, sizeof(buf));
    lv_label_set_text(s_oth_label, buf);
}

static void browser_fill(const char *path);

/* Frees the row's copy of the entry name when the row goes away. */
static void free_user_data(lv_event_t *e)
{
    void *p = lv_event_get_user_data(e);
    if (p != NULL) {
        lv_free(p);
    }
}

static void on_entry(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);
    if (name == NULL) {
        return;
    }

    if (strcmp(name, "..") == 0) {
        /* Walk one level up by trimming the last path component, stopping
         * at the mount point so the browser cannot escape the card. */
        char *slash = strrchr(s_cwd, '/');
        if (slash != NULL && slash != s_cwd) {
            *slash = '\0';
        }
        if (strlen(s_cwd) < strlen(BSP_SD_MOUNT_POINT)) {
            strncpy(s_cwd, BSP_SD_MOUNT_POINT, sizeof(s_cwd) - 1);
        }
    } else {
        char next[sizeof(s_cwd)];
        const int n = snprintf(next, sizeof(next), "%s/%s", s_cwd, name);
        if (n > 0 && (size_t)n < sizeof(next)) {
            strncpy(s_cwd, next, sizeof(s_cwd) - 1);
            s_cwd[sizeof(s_cwd) - 1] = '\0';
        }
    }
    browser_fill(s_cwd);
}

static void browser_fill(const char *path)
{
    lv_obj_clean(s_browser);

    svc_storage_entry_t entries[24];
    const size_t n = svc_storage_list(path, entries, 24);

    /* Offer a way back up unless we are already at the mount point. */
    if (strcmp(path, BSP_SD_MOUNT_POINT) != 0) {
        lv_obj_t *up = ui_menu_row(s_browser, LV_SYMBOL_LEFT, UI_COLOR_TEXT_FAINT,
                                   "..", on_entry, (void *)"..");
        lv_obj_set_height(up, 44);
    }

    if (n == 0) {
        lv_obj_t *lbl = lv_label_create(s_browser);
        lv_label_set_text(lbl, i18n(STR_NONE));
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
        return;
    }

    for (size_t i = 0; i < n; i++) {
        /* The row keeps its own copy of the name: `entries` is a local and
         * the callback fires long after this function returns. */
        char *name = lv_malloc(SVC_STORAGE_NAME_MAX);
        if (name == NULL) {
            continue;
        }
        strncpy(name, entries[i].name, SVC_STORAGE_NAME_MAX - 1);
        name[SVC_STORAGE_NAME_MAX - 1] = '\0';

        lv_obj_t *row = ui_menu_row(s_browser,
                                    entries[i].is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE,
                                    entries[i].is_dir ? UI_COLOR_ORANGE : UI_COLOR_ACCENT,
                                    name,
                                    entries[i].is_dir ? on_entry : NULL,
                                    name);
        lv_obj_set_height(row, 44);
        /* Tie the copy's lifetime to the row. */
        lv_obj_add_event_cb(row, free_user_data, LV_EVENT_DELETE, name);

        if (!entries[i].is_dir) {
            char size[16];
            svc_storage_format_size(entries[i].size, size, sizeof(size));
            lv_obj_t *sz = lv_label_create(row);
            lv_label_set_text(sz, size);
            lv_obj_set_style_text_color(sz, UI_COLOR_TEXT_FAINT, LV_PART_MAIN);
            lv_obj_set_style_text_font(sz, ui_font(UI_FONT_TINY), LV_PART_MAIN);
            lv_obj_align(sz, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }
}

static void on_mount(lv_event_t *e)
{
    (void)e;
    if (svc_storage_sd_info()->mounted) {
        svc_storage_unmount_sd();
        ui_toast(i18n(STR_NO_CARD), UI_COLOR_ORANGE);
    } else if (svc_storage_mount_sd() == ESP_OK) {
        ui_toast(i18n(STR_SD_CARD), UI_COLOR_GREEN);
    } else {
        ui_toast(i18n(STR_NO_CARD), UI_COLOR_RED);
    }
    ui_nav_rebuild();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id != WATCH_EV_SD_STATE || !ui_nav_is_current(UI_SCR_SDCARD)) {
        return;
    }
    if (bsp_display_lock(100)) {
        ui_nav_rebuild();
        bsp_display_unlock();
    }
}

lv_obj_t *scr_sdcard_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_SD_CARD, &body);

    if (!svc_storage_sd_info()->mounted) {
        ui_empty_state(body, LV_SYMBOL_SD_CARD, i18n(STR_NO_CARD), NULL);
        lv_obj_t *btn = ui_button(body, i18n(STR_RETRY), UI_COLOR_ACCENT, on_mount, NULL);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
        ui_screen_subscribe(scr, on_event);
        return scr;
    }

    /* ---- usage ---- */
    lv_obj_t *top = ui_card(body, 150);

    s_ring = ui_ring(top, 110, UI_COLOR_ACCENT, 0, "0%", &s_ring_label);
    lv_obj_align(s_ring, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_font(s_ring_label, ui_font(UI_FONT_TITLE), LV_PART_MAIN);

    lv_obj_t *side = lv_obj_create(top);
    ui_style_plain(side);
    lv_obj_set_size(side, 190, 120);
    lv_obj_align(side, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);

    s_used_label = NULL;
    ui_kv_row(side, i18n(STR_USED), "-", &s_used_label);
    ui_kv_row(side, i18n(STR_IMAGES), "-", &s_img_label);
    ui_kv_row(side, i18n(STR_VIDEOS), "-", &s_vid_label);
    ui_kv_row(side, i18n(STR_OTHERS), "-", &s_oth_label);

    /* ---- browser ---- */
    lv_obj_t *cap = lv_label_create(body);
    lv_label_set_text(cap, i18n(STR_BROWSE_FILES));
    lv_obj_set_style_text_color(cap, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(cap, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);

    s_browser = lv_obj_create(body);
    ui_style_plain(s_browser);
    lv_obj_set_width(s_browser, LV_PCT(100));
    lv_obj_set_height(s_browser, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_browser, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_browser, 6, LV_PART_MAIN);

    ui_button(body, i18n(STR_CLOSE), lv_color_hex(0x1B2636), on_mount, NULL);

    strncpy(s_cwd, BSP_SD_MOUNT_POINT, sizeof(s_cwd) - 1);
    s_cwd[sizeof(s_cwd) - 1] = '\0';

    refresh_usage();
    browser_fill(s_cwd);
    ui_screen_subscribe(scr, on_event);
    return scr;
}
