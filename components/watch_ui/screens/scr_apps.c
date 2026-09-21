/*
 * 04 - Applications.
 *
 * A 3-column grid of coloured app tiles, paged the way the design shows.
 * Everything the watch can do is reachable from here.
 */
#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_power.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char    *icon;
    /* Held as a plain hex value, not an lv_color_t: lv_color_hex() is a static
     * inline, so it is not a constant expression and cannot initialise a
     * static table. Converted at use in app_tile(). */
    uint32_t       color;
    i18n_id_t      label;
    ui_screen_id_t target;
} app_entry_t;

static void on_tile(lv_event_t *e)
{
    const ui_screen_id_t target = (ui_screen_id_t)(uintptr_t)lv_event_get_user_data(e);
    svc_power_notify_activity();
    ui_nav_go(target);
}

static lv_obj_t *app_tile(lv_obj_t *parent, const app_entry_t *entry)
{
    lv_obj_t *cell = lv_obj_create(parent);
    ui_style_plain(cell);
    lv_obj_set_size(cell, 112, 104);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cell, on_tile, LV_EVENT_CLICKED, (void *)(uintptr_t)entry->target);

    /* The rounded colour square that carries the glyph. */
    lv_obj_t *chip = lv_obj_create(cell);
    ui_style_plain(chip);
    lv_obj_set_size(chip, 64, 64);
    lv_obj_set_style_radius(chip, 20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(chip, lv_color_hex(entry->color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(chip, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *ico = lv_label_create(chip);
    lv_label_set_text(ico, entry->icon);
    lv_obj_set_style_text_color(ico, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(ico, ui_font(UI_FONT_TITLE), LV_PART_MAIN);
    lv_obj_center(ico);

    lv_obj_t *lbl = lv_label_create(cell);
    lv_label_set_text(lbl, i18n(entry->label));
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, ui_font_text(UI_FONT_TINY), LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(lbl, 108);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 0);

    return cell;
}

lv_obj_t *scr_apps_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_APPS, &body);

    static const app_entry_t k_apps[] = {
        { LV_SYMBOL_EYE_OPEN, 0x3B82F6, STR_AI_VISION,  UI_SCR_VISION      },
        { LV_SYMBOL_GPS,      0x22C55E, STR_SENSOR,     UI_SCR_SENSOR      },
        { LV_SYMBOL_IMAGE,    0xA855F7, STR_CAMERA,     UI_SCR_CAMERA      },
        { LV_SYMBOL_AUDIO,    0xF59E0B, STR_AUDIO,      UI_SCR_AUDIO       },
        { LV_SYMBOL_WIFI,     0x22D3EE, STR_WIFI,       UI_SCR_WIFI        },
        { LV_SYMBOL_BLUETOOTH,0x2563EB, STR_BLUETOOTH,  UI_SCR_BLUETOOTH   },
        { LV_SYMBOL_SD_CARD,  0x0EA5E9, STR_SD_CARD,    UI_SCR_SDCARD      },
        { LV_SYMBOL_SETTINGS, 0x64748B, STR_SETTINGS,   UI_SCR_SETTINGS    },
        { LV_SYMBOL_CHARGE,   0x16A34A, STR_BATTERY,    UI_SCR_BATTERY     },
        { LV_SYMBOL_DRIVE,    0xEF4444, STR_OBD2,       UI_SCR_OBD2_HOME   },
        { LV_SYMBOL_CHARGE,   0xEC4899, STR_MOTION_GRAPH, UI_SCR_MOTION_GRAPH },
        { LV_SYMBOL_DOWNLOAD, 0x8B5CF6, STR_OTA_UPDATE, UI_SCR_OTA         },
    };

    lv_obj_t *grid = lv_obj_create(body);
    ui_style_plain(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, 6, LV_PART_MAIN);

    for (size_t i = 0; i < sizeof(k_apps) / sizeof(k_apps[0]); i++) {
        app_tile(grid, &k_apps[i]);
    }

    return scr;
}
