/*
 * UI strings in English and Japanese.
 *
 * A flat enum indexes two parallel tables. Screens call i18n() every time
 * they build a label and rebuild themselves on WATCH_EV_LANG_CHANGED, so
 * switching language never needs a reboot.
 *
 * Japanese is limited to the characters in LVGL's bundled
 * lv_font_source_han_sans_sc_16_cjk, which is a fixed 1373-glyph subset.
 * tools/check_i18n_font.ps1 verifies every string in the table against that
 * font; run it after editing any Japanese text or the new characters will
 * render as blanks on the device.
 */
#pragma once

#include "watch_svc/svc_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* ---- shared ---- */
    STR_BACK = 0,
    STR_OK,
    STR_CANCEL,
    STR_ON,
    STR_OFF,
    STR_YES,
    STR_NO,
    STR_CONNECTED,
    STR_NOT_CONNECTED,
    STR_CONNECTING,
    STR_DISCONNECT,
    STR_CONNECT,
    STR_SCANNING,
    STR_NONE,
    STR_UNKNOWN,
    STR_LOADING,
    STR_ERROR,
    STR_RETRY,
    STR_SAVE,
    STR_CLOSE,
    STR_START,
    STR_STOP,
    STR_ENABLED,
    STR_DISABLED,
    STR_NOT_AVAILABLE,

    /* ---- 02 watch face / 03 dashboard ---- */
    STR_STEPS,
    STR_TEMPERATURE,
    STR_BATTERY,
    STR_ACTIVITY,
    STR_GOAL,
    STR_TODAY,

    /* ---- 04 applications ---- */
    STR_APPS,
    STR_AI_VISION,
    STR_SENSOR,
    STR_CAMERA,
    STR_AUDIO,
    STR_WIFI,
    STR_BLUETOOTH,
    STR_SD_CARD,
    STR_SETTINGS,
    STR_MORE,

    /* ---- 05 sensor / 06 motion graph ---- */
    STR_MOTION_SENSOR,
    STR_ACCELEROMETER,
    STR_GYROSCOPE,
    STR_MOTION_GRAPH,
    STR_ACCEL,
    STR_GYRO,
    STR_SENSOR_MISSING,

    /* ---- 07 battery ---- */
    STR_VOLTAGE,
    STR_CURRENT,
    STR_POWER,
    STR_CHARGING,
    STR_POWER_SAVING,
    STR_SYSTEM_VOLTAGE,
    STR_CHARGE_STATE,
    STR_NO_BATTERY,
    STR_USB_POWER,

    /* ---- 08 wi-fi / 17 network ---- */
    STR_SIGNAL,
    STR_IP_ADDRESS,
    STR_SCAN_NETWORKS,
    STR_AVAILABLE_NETWORKS,
    STR_ADD_NETWORK,
    STR_PASSWORD,
    STR_NETWORK_SETTINGS,
    STR_FORGET,
    STR_NO_NETWORKS,

    /* ---- 09 bluetooth ---- */
    STR_DEVICES,
    STR_SCAN,
    STR_NO_DEVICES,

    /* ---- 10 audio ---- */
    STR_RECORDING,
    STR_MIC_LEVEL,
    STR_VOLUME,
    STR_MIC_GAIN,
    STR_PLAY,
    STR_RECORD,

    /* ---- 11 sd card ---- */
    STR_USED,
    STR_FREE,
    STR_TOTAL,
    STR_IMAGES,
    STR_VIDEOS,
    STR_OTHERS,
    STR_BROWSE_FILES,
    STR_NO_CARD,
    STR_STORAGE,

    /* ---- 12 camera / 13 vision / 14 detail ---- */
    STR_PHOTO,
    STR_VIDEO,
    STR_CAPTURE,
    STR_NO_CAMERA,
    STR_CAMERA_HINT,
    STR_OBJECTS,
    STR_DETECTION_DETAIL,
    STR_CONFIDENCE,
    STR_NO_DETECTIONS,
    STR_OBJECT,

    /* ---- 15 settings / 16 display ---- */
    STR_DISPLAY,
    STR_SENSORS,
    STR_SYSTEM,
    STR_ABOUT,
    STR_OTA_UPDATE,
    STR_POWER_OFF,
    STR_BRIGHTNESS,
    STR_AUTO_TIMEOUT,
    STR_ALWAYS_ON,
    STR_WATCH_FACE,
    STR_LANGUAGE,
    STR_TIME,
    STR_FACTORY_RESET,

    /* ---- 18 about ---- */
    STR_VERSION,
    STR_BUILD,
    STR_CHIP,
    STR_FLASH,
    STR_RAM,
    STR_FREE_HEAP,
    STR_UPTIME,

    /* ---- 19 ota ---- */
    STR_NEW_VERSION,
    STR_UPDATE_AVAILABLE,
    STR_DOWNLOAD,
    STR_LATER,
    STR_CHECKING,
    STR_UP_TO_DATE,
    STR_INSTALLING,
    STR_REBOOTING,
    STR_UPDATE_FAILED,
    STR_NEEDS_WIFI,

    /* ---- 20 power off ---- */
    STR_SLIDE_TO_POWER_OFF,
    STR_RESTART,

    /* ---- 21-25 obd2 ---- */
    STR_OBD2,
    STR_OBD2_TITLE,
    STR_LIVE_DATA,
    STR_TROUBLE_CODES,
    STR_FREEZE_FRAME,
    STR_VEHICLE_STATUS,
    STR_ADAPTER,
    STR_NO_ADAPTER,
    STR_SEARCHING_ADAPTER,
    STR_ENGINE_LOAD,
    STR_COOLANT_TEMP,
    STR_RPM,
    STR_VEHICLE_SPEED,
    STR_INTAKE_TEMP,
    STR_FUEL_LEVEL,
    STR_THROTTLE,
    STR_CLEAR_CODES,
    STR_NO_CODES,
    STR_STORED,
    STR_PENDING,
    STR_ENGINE,
    STR_ABS,
    STR_SRS,
    STR_TRANSMISSION,
    STR_STATUS_OK,
    STR_STATUS_WARNING,
    STR_STATUS_FAULT,
    STR_NO_FREEZE_FRAME,
    STR_DRIVE_SAFER,

    /* ---- 26 time ---- */
    STR_TIME_SETTINGS,
    STR_DATE,
    STR_24_HOUR,
    STR_TIME_ZONE,
    STR_SYNC_NETWORK,
    STR_SET_MANUALLY,
    STR_HOUR,
    STR_MINUTE,
    STR_YEAR,
    STR_MONTH,
    STR_DAY,

    /* ---- 27 language ---- */
    STR_ENGLISH,
    STR_JAPANESE,

    STR_COUNT
} i18n_id_t;

/** @brief The string for @p id in the current language. Never returns NULL. */
const char *i18n(i18n_id_t id);

/** @brief The string for @p id in a specific language. */
const char *i18n_in(i18n_id_t id, watch_lang_t lang);

/** @brief Switch language and post WATCH_EV_LANG_CHANGED. */
void i18n_set_lang(watch_lang_t lang);

/** @brief The language in force right now. */
watch_lang_t i18n_get_lang(void);

/** @brief True when the current language needs the CJK font. */
bool i18n_needs_cjk_font(void);

#ifdef __cplusplus
}
#endif
