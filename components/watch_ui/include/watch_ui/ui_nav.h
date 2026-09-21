/*
 * Screen registry and navigation.
 *
 * Screens are built on demand and destroyed when navigated away from, so
 * only the live screen costs memory. A back stack remembers the route.
 *
 * Threading: everything here must be called with the LVGL lock held.
 * Service events arrive on the event task, so a screen's handler has to
 * take the lock itself and re-check ui_nav_is_current() before touching
 * its widgets - by the time it runs, the user may have navigated away and
 * the objects it captured may be gone.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SCR_BOOT = 0,        /* 01 */
    UI_SCR_WATCHFACE,       /* 02 */
    UI_SCR_HOME,            /* 03 */
    UI_SCR_APPS,            /* 04 */
    UI_SCR_SENSOR,          /* 05 */
    UI_SCR_MOTION_GRAPH,    /* 06 */
    UI_SCR_BATTERY,         /* 07 */
    UI_SCR_WIFI,            /* 08 */
    UI_SCR_BLUETOOTH,       /* 09 */
    UI_SCR_AUDIO,           /* 10 */
    UI_SCR_SDCARD,          /* 11 */
    UI_SCR_CAMERA,          /* 12 */
    UI_SCR_VISION,          /* 13 */
    UI_SCR_DETECTION,       /* 14 */
    UI_SCR_SETTINGS,        /* 15 */
    UI_SCR_DISPLAY,         /* 16 */
    UI_SCR_NETWORK,         /* 17 */
    UI_SCR_ABOUT,           /* 18 */
    UI_SCR_OTA,             /* 19 */
    UI_SCR_POWEROFF,        /* 20 */
    UI_SCR_OBD2_HOME,       /* 21 */
    UI_SCR_OBD2_LIVE,       /* 22 */
    UI_SCR_OBD2_DTC,        /* 23 */
    UI_SCR_OBD2_FREEZE,     /* 24 */
    UI_SCR_OBD2_STATUS,     /* 25 */
    UI_SCR_TIME,            /* 26 */
    UI_SCR_LANGUAGE,        /* 27 */
    UI_SCR_COUNT
} ui_screen_id_t;

/** @brief Builds a screen and returns its root object. */
typedef lv_obj_t *(*ui_screen_create_fn)(void);

/** @brief Set up the navigator and show @p first. */
void ui_nav_init(ui_screen_id_t first);

/** @brief Go to @p id, remembering where we came from. */
void ui_nav_go(ui_screen_id_t id);

/** @brief Return to the previous screen. No-op at the root. */
void ui_nav_back(void);

/** @brief Go to @p id and forget the history - used for the home screens. */
void ui_nav_replace(ui_screen_id_t id);

/** @brief The screen on display right now. */
ui_screen_id_t ui_nav_current(void);

/** @brief Whether @p id is the screen on display. */
bool ui_nav_is_current(ui_screen_id_t id);

/** @brief True when there is somewhere to go back to. */
bool ui_nav_can_go_back(void);

/** @brief Rebuild the current screen, e.g. after the language changed. */
void ui_nav_rebuild(void);

/**
 * @brief Subscribe @p handler to every watch event for as long as @p scr lives.
 *
 * Unsubscribes automatically when the screen is deleted, which removes the
 * single most common way to crash this kind of UI: an event arriving for a
 * screen that has already gone.
 */
void ui_screen_subscribe(lv_obj_t *scr, esp_event_handler_t handler);

/**
 * @brief A repeating timer that lives exactly as long as @p scr.
 *
 * Deleted with the screen, which is the other classic way this kind of UI
 * crashes: a 1 Hz refresh timer still firing against widgets that were
 * freed when the user navigated away.
 *
 * The callback runs on the LVGL task, so it may touch widgets directly and
 * must not take the LVGL lock.
 */
lv_timer_t *ui_screen_add_timer(lv_obj_t *scr, lv_timer_cb_t cb, uint32_t period_ms);

/**
 * @brief Make swiping right on @p scr go back.
 *
 * Applied by ui_widgets_header(), so screens with a header get it for free.
 */
void ui_screen_enable_back_gesture(lv_obj_t *scr);

#ifdef __cplusplus
}
#endif
