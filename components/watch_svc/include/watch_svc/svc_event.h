/*
 * The watch event bus.
 *
 * Services run on their own tasks and never touch LVGL. They post here; the
 * UI subscribes and takes the LVGL lock inside its handler. That is the only
 * sanctioned path from a driver to a widget - anything else races the LVGL
 * task and eventually corrupts the display list.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(WATCH_EVENT);

typedef enum {
    /* Power. Payload: axp2101_status_t */
    WATCH_EV_POWER_CHANGED = 1,
    /* Battery fell below the warning threshold. Payload: uint8_t percent */
    WATCH_EV_LOW_BATTERY,
    /* Display went dark / came back. No payload. */
    WATCH_EV_DISPLAY_SLEEP,
    WATCH_EV_DISPLAY_WAKE,

    /* Motion. Payload: svc_sensors_sample_t */
    WATCH_EV_SENSOR_SAMPLE,
    /* Payload: uint32_t steps */
    WATCH_EV_STEPS_CHANGED,

    /* Wi-Fi. Payload: svc_wifi_status_t */
    WATCH_EV_WIFI_STATE,
    /* Scan results are ready to read with svc_wifi_get_scan(). No payload. */
    WATCH_EV_WIFI_SCAN_DONE,

    /* Bluetooth. Payload: svc_ble_status_t */
    WATCH_EV_BLE_STATE,
    /* Scan results ready via svc_ble_get_scan(). No payload. */
    WATCH_EV_BLE_SCAN_DONE,

    /* OBD2. Payload: svc_obd2_status_t */
    WATCH_EV_OBD2_STATE,
    /* A fresh set of live PIDs. Payload: svc_obd2_live_t */
    WATCH_EV_OBD2_LIVE,
    /* Trouble codes were re-read. No payload; call svc_obd2_get_dtcs(). */
    WATCH_EV_OBD2_DTC,

    /* Clock was set from SNTP or by the user. No payload. */
    WATCH_EV_TIME_SYNCED,

    /* Payload: watch_setting_key_t */
    WATCH_EV_SETTINGS_CHANGED,
    /* Language switched. Every screen must rebuild its text. Payload: watch_lang_t */
    WATCH_EV_LANG_CHANGED,

    /* SD card mounted or removed. Payload: bool mounted */
    WATCH_EV_SD_STATE,

    /* OTA. Payload: svc_ota_progress_t */
    WATCH_EV_OTA_PROGRESS,

    /* A camera frame is decoded and ready. No payload; the UI reads the
     * shared frame under svc_camera_lock(). */
    WATCH_EV_CAMERA_FRAME,
    /* Detector finished a frame. Payload: svc_vision_result_t */
    WATCH_EV_VISION_RESULT,

    /* Audio capture level, for the recording meter. Payload: uint8_t 0-100 */
    WATCH_EV_AUDIO_LEVEL,
} watch_event_id_t;

/** @brief Create the event loop. Call once, before any other service. */
esp_err_t svc_event_init(void);

/** @brief The loop services post to and the UI subscribes on. */
esp_event_loop_handle_t svc_event_loop(void);

/**
 * @brief Post an event to the watch loop.
 *
 * Never blocks: a full queue drops the event rather than stalling the caller.
 * Every payload here is a periodic status update, so dropping one is always
 * better than holding up a driver task.
 */
esp_err_t svc_event_post(watch_event_id_t id, const void *data, size_t len);

/** @brief Subscribe. Pass @p id as -1 to receive every watch event. */
esp_err_t svc_event_subscribe(int32_t id, esp_event_handler_t handler, void *arg);

/** @brief Unsubscribe a handler registered with svc_event_subscribe(). */
esp_err_t svc_event_unsubscribe(int32_t id, esp_event_handler_t handler);

#ifdef __cplusplus
}
#endif
