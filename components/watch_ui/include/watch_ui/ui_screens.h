/*
 * The screen constructors, one per file under screens/.
 *
 * Each returns a fresh LVGL screen object. The navigator owns it from that
 * point and deletes it on the way out, so a constructor must not stash the
 * pointer anywhere that outlives the screen.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *scr_boot_create(void);          /* 01 boot / splash */
lv_obj_t *scr_watchface_create(void);     /* 02 watch face */
lv_obj_t *scr_home_create(void);          /* 03 home dashboard */
lv_obj_t *scr_apps_create(void);          /* 04 applications */
lv_obj_t *scr_sensor_create(void);        /* 05 motion sensor */
lv_obj_t *scr_motion_graph_create(void);  /* 06 motion graph */
lv_obj_t *scr_battery_create(void);       /* 07 battery */
lv_obj_t *scr_wifi_create(void);          /* 08 wi-fi */
lv_obj_t *scr_bluetooth_create(void);     /* 09 bluetooth */
lv_obj_t *scr_audio_create(void);         /* 10 audio */
lv_obj_t *scr_sdcard_create(void);        /* 11 sd card */
lv_obj_t *scr_camera_create(void);        /* 12 camera */
lv_obj_t *scr_vision_create(void);        /* 13 ai vision */
lv_obj_t *scr_detection_create(void);     /* 14 detection detail */
lv_obj_t *scr_settings_create(void);      /* 15 settings */
lv_obj_t *scr_display_create(void);       /* 16 display */
lv_obj_t *scr_network_create(void);       /* 17 network settings */
lv_obj_t *scr_about_create(void);         /* 18 about */
lv_obj_t *scr_ota_create(void);           /* 19 ota update */
lv_obj_t *scr_poweroff_create(void);      /* 20 power off */
lv_obj_t *scr_obd2_home_create(void);     /* 21 obd2 home */
lv_obj_t *scr_obd2_live_create(void);     /* 22 live data */
lv_obj_t *scr_obd2_dtc_create(void);      /* 23 trouble codes */
lv_obj_t *scr_obd2_freeze_create(void);   /* 24 freeze frame */
lv_obj_t *scr_obd2_status_create(void);   /* 25 vehicle status */
lv_obj_t *scr_time_create(void);          /* 26 time settings */
lv_obj_t *scr_language_create(void);      /* 27 language */

#ifdef __cplusplus
}
#endif
