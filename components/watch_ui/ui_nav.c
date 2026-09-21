#include "watch_ui/ui_nav.h"
#include "watch_ui/ui_screens.h"
#include "watch_ui/ui_theme.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_power.h"

#include <string.h>
#include "esp_log.h"

static const char *TAG = "ui_nav";

#define NAV_STACK_MAX 8

static const ui_screen_create_fn k_screens[UI_SCR_COUNT] = {
    [UI_SCR_BOOT]         = scr_boot_create,
    [UI_SCR_WATCHFACE]    = scr_watchface_create,
    [UI_SCR_HOME]         = scr_home_create,
    [UI_SCR_APPS]         = scr_apps_create,
    [UI_SCR_SENSOR]       = scr_sensor_create,
    [UI_SCR_MOTION_GRAPH] = scr_motion_graph_create,
    [UI_SCR_BATTERY]      = scr_battery_create,
    [UI_SCR_WIFI]         = scr_wifi_create,
    [UI_SCR_BLUETOOTH]    = scr_bluetooth_create,
    [UI_SCR_AUDIO]        = scr_audio_create,
    [UI_SCR_SDCARD]       = scr_sdcard_create,
    [UI_SCR_CAMERA]       = scr_camera_create,
    [UI_SCR_VISION]       = scr_vision_create,
    [UI_SCR_DETECTION]    = scr_detection_create,
    [UI_SCR_SETTINGS]     = scr_settings_create,
    [UI_SCR_DISPLAY]      = scr_display_create,
    [UI_SCR_NETWORK]      = scr_network_create,
    [UI_SCR_ABOUT]        = scr_about_create,
    [UI_SCR_OTA]          = scr_ota_create,
    [UI_SCR_POWEROFF]     = scr_poweroff_create,
    [UI_SCR_OBD2_HOME]    = scr_obd2_home_create,
    [UI_SCR_OBD2_LIVE]    = scr_obd2_live_create,
    [UI_SCR_OBD2_DTC]     = scr_obd2_dtc_create,
    [UI_SCR_OBD2_FREEZE]  = scr_obd2_freeze_create,
    [UI_SCR_OBD2_STATUS]  = scr_obd2_status_create,
    [UI_SCR_TIME]         = scr_time_create,
    [UI_SCR_LANGUAGE]     = scr_language_create,
};

static ui_screen_id_t s_stack[NAV_STACK_MAX];
static int            s_depth;          /* entries below the current screen */
static ui_screen_id_t s_current = UI_SCR_BOOT;

/* --------------------------------------------------------------- internals */

static void load_screen(ui_screen_id_t id, lv_screen_load_anim_t anim)
{
    if (id >= UI_SCR_COUNT || k_screens[id] == NULL) {
        ESP_LOGE(TAG, "screen %d is not registered", (int)id);
        return;
    }

    lv_obj_t *scr = k_screens[id]();
    if (scr == NULL) {
        ESP_LOGE(TAG, "screen %d failed to build", (int)id);
        return;
    }

    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    s_current = id;
    /* auto_del deletes the screen being replaced once the animation ends,
     * which is what keeps only one screen alive at a time. */
    lv_screen_load_anim(scr, anim, UI_ANIM_MS, 0, true);
}

/* ---------------------------------------------------------------------- api */

void ui_nav_init(ui_screen_id_t first)
{
    s_depth = 0;
    s_current = first;

    lv_obj_t *scr = k_screens[first]();
    if (scr != NULL) {
        lv_obj_set_style_bg_color(scr, UI_COLOR_BG, LV_PART_MAIN);
        lv_screen_load(scr);
    }
}

void ui_nav_go(ui_screen_id_t id)
{
    if (id == s_current) {
        return;
    }
    if (s_depth < NAV_STACK_MAX) {
        s_stack[s_depth++] = s_current;
    } else {
        /* Deep enough that the oldest entry is not worth keeping; drop it
         * so navigation never wedges. */
        memmove(&s_stack[0], &s_stack[1], (NAV_STACK_MAX - 1) * sizeof(s_stack[0]));
        s_stack[NAV_STACK_MAX - 1] = s_current;
    }
    svc_power_notify_activity();
    load_screen(id, LV_SCREEN_LOAD_ANIM_MOVE_LEFT);
}

void ui_nav_back(void)
{
    if (s_depth <= 0) {
        return;
    }
    const ui_screen_id_t prev = s_stack[--s_depth];
    svc_power_notify_activity();
    load_screen(prev, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
}

void ui_nav_replace(ui_screen_id_t id)
{
    s_depth = 0;
    svc_power_notify_activity();
    load_screen(id, LV_SCREEN_LOAD_ANIM_FADE_IN);
}

ui_screen_id_t ui_nav_current(void)
{
    return s_current;
}

bool ui_nav_is_current(ui_screen_id_t id)
{
    return s_current == id;
}

bool ui_nav_can_go_back(void)
{
    return s_depth > 0;
}

void ui_nav_rebuild(void)
{
    /* No animation: this is a redraw of the same screen, not a move. */
    load_screen(s_current, LV_SCREEN_LOAD_ANIM_NONE);
}

/* ------------------------------------------------------- screen lifecycle */

static void on_screen_deleted(lv_event_t *e)
{
    esp_event_handler_t handler = (esp_event_handler_t)lv_event_get_user_data(e);
    if (handler != NULL) {
        svc_event_unsubscribe(ESP_EVENT_ANY_ID, handler);
    }
}

void ui_screen_subscribe(lv_obj_t *scr, esp_event_handler_t handler)
{
    if (scr == NULL || handler == NULL) {
        return;
    }
    svc_event_subscribe(ESP_EVENT_ANY_ID, handler, NULL);
    lv_obj_add_event_cb(scr, on_screen_deleted, LV_EVENT_DELETE, (void *)handler);
}

static void on_timer_owner_deleted(lv_event_t *e)
{
    lv_timer_t *timer = (lv_timer_t *)lv_event_get_user_data(e);
    if (timer != NULL) {
        lv_timer_delete(timer);
    }
}

lv_timer_t *ui_screen_add_timer(lv_obj_t *scr, lv_timer_cb_t cb, uint32_t period_ms)
{
    if (scr == NULL || cb == NULL) {
        return NULL;
    }
    lv_timer_t *timer = lv_timer_create(cb, period_ms, NULL);
    if (timer == NULL) {
        return NULL;
    }
    lv_obj_add_event_cb(scr, on_timer_owner_deleted, LV_EVENT_DELETE, timer);
    return timer;
}

static void on_gesture(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    svc_power_notify_activity();

    if (dir == LV_DIR_RIGHT && ui_nav_can_go_back()) {
        /* Stop the gesture here so a scrollable child does not also act on
         * it and scroll sideways while the screen slides away. */
        lv_indev_wait_release(indev);
        ui_nav_back();
    }
}

void ui_screen_enable_back_gesture(lv_obj_t *scr)
{
    if (scr == NULL) {
        return;
    }
    lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, NULL);
}
