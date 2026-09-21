/*
 * Persisted settings.
 *
 * One struct, loaded from NVS at boot and written back on change. Writers go
 * through svc_settings_set_*(), which validates, stores, and posts
 * WATCH_EV_SETTINGS_CHANGED so the rest of the system reacts without the
 * caller having to know who cares.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WATCH_LANG_EN = 0,
    WATCH_LANG_JP = 1,
    WATCH_LANG_COUNT
} watch_lang_t;

typedef enum {
    WATCH_SET_BRIGHTNESS = 1,
    WATCH_SET_IDLE_DIM_SEC,
    WATCH_SET_IDLE_OFF_SEC,
    WATCH_SET_ALWAYS_ON,
    WATCH_SET_TIME_24H,
    WATCH_SET_TIMEZONE,
    WATCH_SET_NTP_ENABLE,
    WATCH_SET_LANG,
    WATCH_SET_WATCHFACE,
    WATCH_SET_WIFI_ENABLE,
    WATCH_SET_BLE_ENABLE,
    WATCH_SET_VOLUME,
    WATCH_SET_MIC_GAIN,
    WATCH_SET_IMU_ENABLE,
    WATCH_SET_STEP_GOAL,
    WATCH_SET_ALL,          /* emitted after a load or a factory reset */
} watch_setting_key_t;

#define WATCH_TZ_MAX_LEN   40
#define WATCH_SSID_MAX_LEN 33
#define WATCH_PASS_MAX_LEN 65

typedef struct {
    uint8_t      brightness;      /* 1-100, the AMOLED 0x51 level */
    uint16_t     idle_dim_sec;    /* 0 disables dimming */
    uint16_t     idle_off_sec;    /* 0 keeps the screen on forever */
    bool         always_on;       /* dim clock instead of a blank screen */
    bool         time_24h;
    char         tz[WATCH_TZ_MAX_LEN];  /* POSIX TZ, e.g. "JST-9" */
    bool         ntp_enable;
    watch_lang_t lang;
    uint8_t      watchface;       /* 0-2 */
    bool         wifi_enable;
    bool         ble_enable;
    uint8_t      volume;          /* 0-100 */
    uint8_t      mic_gain;        /* 0-100 */
    bool         imu_enable;
    uint32_t     step_goal;
} watch_settings_t;

/** @brief Open NVS and load the settings, filling in defaults for anything absent. */
esp_err_t svc_settings_init(void);

/** @brief The live settings. Read-only: never write through this pointer. */
const watch_settings_t *svc_settings_get(void);

esp_err_t svc_settings_set_brightness(uint8_t percent);
esp_err_t svc_settings_set_idle_dim_sec(uint16_t sec);
esp_err_t svc_settings_set_idle_off_sec(uint16_t sec);
esp_err_t svc_settings_set_always_on(bool on);
esp_err_t svc_settings_set_time_24h(bool on);
esp_err_t svc_settings_set_timezone(const char *posix_tz);
esp_err_t svc_settings_set_ntp_enable(bool on);
esp_err_t svc_settings_set_lang(watch_lang_t lang);
esp_err_t svc_settings_set_watchface(uint8_t index);
esp_err_t svc_settings_set_wifi_enable(bool on);
esp_err_t svc_settings_set_ble_enable(bool on);
esp_err_t svc_settings_set_volume(uint8_t percent);
esp_err_t svc_settings_set_mic_gain(uint8_t percent);
esp_err_t svc_settings_set_imu_enable(bool on);
esp_err_t svc_settings_set_step_goal(uint32_t steps);

/** @brief Remember the last Wi-Fi network so the watch reconnects on boot. */
esp_err_t svc_settings_save_wifi(const char *ssid, const char *password);

/**
 * @brief Read back the saved Wi-Fi credentials.
 *
 * @return ESP_ERR_NOT_FOUND when nothing has been saved yet.
 */
esp_err_t svc_settings_load_wifi(char *ssid, size_t ssid_len,
                                 char *password, size_t pass_len);

/** @brief Remember the BLE address of the paired OBD2 adapter. */
esp_err_t svc_settings_save_obd2_addr(const uint8_t addr[6], uint8_t addr_type);

/** @brief Read back the paired OBD2 adapter. ESP_ERR_NOT_FOUND if none. */
esp_err_t svc_settings_load_obd2_addr(uint8_t addr[6], uint8_t *addr_type);

/** @brief Erase everything and reload defaults. */
esp_err_t svc_settings_factory_reset(void);

#ifdef __cplusplus
}
#endif
