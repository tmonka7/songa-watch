#include "watch_svc/svc_settings.h"
#include "watch_svc/svc_event.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "svc_settings";

#define NVS_NAMESPACE "watch"
#define NVS_BLOB_KEY  "settings"
#define NVS_WIFI_SSID "wifi_ssid"
#define NVS_WIFI_PASS "wifi_pass"
#define NVS_OBD_ADDR  "obd_addr"
#define NVS_OBD_TYPE  "obd_type"

/* Bumped whenever the struct layout changes; a mismatch falls back to
 * defaults rather than reinterpreting old bytes. */
#define SETTINGS_VERSION 2

static watch_settings_t s_cfg;
static nvs_handle_t     s_nvs;
static bool             s_ready;

static void load_defaults(void)
{
    s_cfg = (watch_settings_t){
        .brightness   = 70,
        .idle_dim_sec = CONFIG_WATCH_IDLE_DIM_SEC,
        .idle_off_sec = CONFIG_WATCH_IDLE_SLEEP_SEC,
        .always_on    = false,
        .time_24h     = true,
        /* Off by default. This watch is built to run without a network, so
         * the clock is set by hand on the Time screen; turning SNTP on is an
         * opt-in for the times a Wi-Fi network is actually around. */
        .ntp_enable   = false,
        .lang         = WATCH_LANG_EN,
        .watchface    = 0,
        .wifi_enable  = false,
        .ble_enable   = false,
        .volume       = 60,
        .mic_gain     = 50,
        .imu_enable   = true,
        .wake_on_raise = true,
        .step_goal    = 10000,
    };
    strncpy(s_cfg.tz, "UTC0", sizeof(s_cfg.tz) - 1);
}

static void clamp_settings(void)
{
    if (s_cfg.brightness < 1)   { s_cfg.brightness = 1; }
    if (s_cfg.brightness > 100) { s_cfg.brightness = 100; }
    if (s_cfg.volume > 100)     { s_cfg.volume = 100; }
    if (s_cfg.mic_gain > 100)   { s_cfg.mic_gain = 100; }
    if (s_cfg.watchface > 2)    { s_cfg.watchface = 0; }
    if (s_cfg.lang >= WATCH_LANG_COUNT) { s_cfg.lang = WATCH_LANG_EN; }
    if (s_cfg.step_goal == 0 || s_cfg.step_goal > 200000) { s_cfg.step_goal = 10000; }
    s_cfg.tz[sizeof(s_cfg.tz) - 1] = '\0';
    if (s_cfg.tz[0] == '\0') {
        strncpy(s_cfg.tz, "UTC0", sizeof(s_cfg.tz) - 1);
    }
}

/* The blob is prefixed with a version word so an upgrade that changes the
 * struct cannot silently read garbage out of the old bytes. */
typedef struct {
    uint32_t         version;
    watch_settings_t cfg;
} stored_blob_t;

static esp_err_t persist(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const stored_blob_t blob = { .version = SETTINGS_VERSION, .cfg = s_cfg };
    ESP_RETURN_ON_ERROR(nvs_set_blob(s_nvs, NVS_BLOB_KEY, &blob, sizeof(blob)),
                        TAG, "nvs_set_blob");
    return nvs_commit(s_nvs);
}

static esp_err_t commit_and_notify(watch_setting_key_t key)
{
    const esp_err_t err = persist();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "could not persist setting %d: %s", (int)key, esp_err_to_name(err));
    }
    svc_event_post(WATCH_EV_SETTINGS_CHANGED, &key, sizeof(key));
    return err;
}

esp_err_t svc_settings_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erasing, doing it");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init");

    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs), TAG, "nvs open");
    s_ready = true;

    load_defaults();

    stored_blob_t blob;
    size_t len = sizeof(blob);
    err = nvs_get_blob(s_nvs, NVS_BLOB_KEY, &blob, &len);
    if (err == ESP_OK && len == sizeof(blob) && blob.version == SETTINGS_VERSION) {
        s_cfg = blob.cfg;
        ESP_LOGI(TAG, "settings loaded");
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no saved settings, using defaults");
        (void)persist();
    } else {
        ESP_LOGW(TAG, "stored settings unusable (%s), using defaults", esp_err_to_name(err));
        (void)persist();
    }

    clamp_settings();
    return ESP_OK;
}

const watch_settings_t *svc_settings_get(void)
{
    return &s_cfg;
}

/* --------------------------------------------------------------- setters */

#define SETTER(fn, key, expr)                              \
    esp_err_t fn                                           \
    {                                                      \
        expr;                                              \
        clamp_settings();                                  \
        return commit_and_notify(key);                     \
    }

SETTER(svc_settings_set_brightness(uint8_t percent),
       WATCH_SET_BRIGHTNESS, s_cfg.brightness = percent)
SETTER(svc_settings_set_idle_dim_sec(uint16_t sec),
       WATCH_SET_IDLE_DIM_SEC, s_cfg.idle_dim_sec = sec)
SETTER(svc_settings_set_idle_off_sec(uint16_t sec),
       WATCH_SET_IDLE_OFF_SEC, s_cfg.idle_off_sec = sec)
SETTER(svc_settings_set_always_on(bool on),
       WATCH_SET_ALWAYS_ON, s_cfg.always_on = on)
SETTER(svc_settings_set_time_24h(bool on),
       WATCH_SET_TIME_24H, s_cfg.time_24h = on)
SETTER(svc_settings_set_ntp_enable(bool on),
       WATCH_SET_NTP_ENABLE, s_cfg.ntp_enable = on)
SETTER(svc_settings_set_watchface(uint8_t index),
       WATCH_SET_WATCHFACE, s_cfg.watchface = index)
SETTER(svc_settings_set_wifi_enable(bool on),
       WATCH_SET_WIFI_ENABLE, s_cfg.wifi_enable = on)
SETTER(svc_settings_set_ble_enable(bool on),
       WATCH_SET_BLE_ENABLE, s_cfg.ble_enable = on)
SETTER(svc_settings_set_volume(uint8_t percent),
       WATCH_SET_VOLUME, s_cfg.volume = percent)
SETTER(svc_settings_set_mic_gain(uint8_t percent),
       WATCH_SET_MIC_GAIN, s_cfg.mic_gain = percent)
SETTER(svc_settings_set_imu_enable(bool on),
       WATCH_SET_IMU_ENABLE, s_cfg.imu_enable = on)
SETTER(svc_settings_set_wake_on_raise(bool on),
       WATCH_SET_WAKE_ON_RAISE, s_cfg.wake_on_raise = on)
SETTER(svc_settings_set_step_goal(uint32_t steps),
       WATCH_SET_STEP_GOAL, s_cfg.step_goal = steps)

#undef SETTER

esp_err_t svc_settings_set_lang(watch_lang_t lang)
{
    if (lang >= WATCH_LANG_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg.lang = lang;
    return commit_and_notify(WATCH_SET_LANG);
}

esp_err_t svc_settings_set_timezone(const char *posix_tz)
{
    if (posix_tz == NULL || posix_tz[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_cfg.tz, posix_tz, sizeof(s_cfg.tz) - 1);
    s_cfg.tz[sizeof(s_cfg.tz) - 1] = '\0';
    return commit_and_notify(WATCH_SET_TIMEZONE);
}

/* ------------------------------------------------------- wi-fi credentials */

esp_err_t svc_settings_save_wifi(const char *ssid, const char *password)
{
    if (!s_ready || ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(nvs_set_str(s_nvs, NVS_WIFI_SSID, ssid), TAG, "save ssid");
    ESP_RETURN_ON_ERROR(nvs_set_str(s_nvs, NVS_WIFI_PASS, (password != NULL) ? password : ""),
                        TAG, "save pass");
    return nvs_commit(s_nvs);
}

esp_err_t svc_settings_load_wifi(char *ssid, size_t ssid_len,
                                 char *password, size_t pass_len)
{
    if (!s_ready || ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t n = ssid_len;
    esp_err_t err = nvs_get_str(s_nvs, NVS_WIFI_SSID, ssid, &n);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    n = pass_len;
    if (nvs_get_str(s_nvs, NVS_WIFI_PASS, password, &n) != ESP_OK) {
        password[0] = '\0';
    }
    return ESP_OK;
}

/* ------------------------------------------------------------ obd2 adapter */

esp_err_t svc_settings_save_obd2_addr(const uint8_t addr[6], uint8_t addr_type)
{
    if (!s_ready || addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(nvs_set_blob(s_nvs, NVS_OBD_ADDR, addr, 6), TAG, "save obd addr");
    ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs, NVS_OBD_TYPE, addr_type), TAG, "save obd type");
    return nvs_commit(s_nvs);
}

esp_err_t svc_settings_load_obd2_addr(uint8_t addr[6], uint8_t *addr_type)
{
    if (!s_ready || addr == NULL || addr_type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t n = 6;
    if (nvs_get_blob(s_nvs, NVS_OBD_ADDR, addr, &n) != ESP_OK || n != 6) {
        return ESP_ERR_NOT_FOUND;
    }
    if (nvs_get_u8(s_nvs, NVS_OBD_TYPE, addr_type) != ESP_OK) {
        *addr_type = 0;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ reset */

esp_err_t svc_settings_factory_reset(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGW(TAG, "factory reset");
    (void)nvs_erase_all(s_nvs);
    (void)nvs_commit(s_nvs);
    load_defaults();
    clamp_settings();
    return commit_and_notify(WATCH_SET_ALL);
}
