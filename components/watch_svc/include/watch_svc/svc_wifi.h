/*
 * Wi-Fi station.
 *
 * Scanning and connecting for the Wi-Fi and Network Settings screens, plus
 * the uplink SNTP and OTA need. Credentials live in NVS via svc_settings.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "watch_svc/svc_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_WIFI_MAX_SCAN 16

typedef enum {
    SVC_WIFI_OFF = 0,
    SVC_WIFI_DISCONNECTED,
    SVC_WIFI_CONNECTING,
    SVC_WIFI_CONNECTED,
    SVC_WIFI_FAILED,
} svc_wifi_state_t;

typedef struct {
    svc_wifi_state_t state;
    char             ssid[WATCH_SSID_MAX_LEN];
    int8_t           rssi;          /* dBm */
    uint8_t          bars;          /* 0-4, for the signal icon */
    char             ip[16];
    bool             scanning;
} svc_wifi_status_t;

typedef struct {
    char    ssid[WATCH_SSID_MAX_LEN];
    int8_t  rssi;
    uint8_t bars;
    bool    secured;
} svc_wifi_ap_t;

/** @brief Initialise netif and the Wi-Fi driver. Does not switch the radio on. */
esp_err_t svc_wifi_init(void);

/**
 * @brief Turn the radio on or off.
 *
 * Off means esp_wifi_stop(), which is what makes idle current acceptable -
 * a station left associated costs far more than the watch can afford.
 */
esp_err_t svc_wifi_set_enabled(bool enable);

/** @brief Current link state. */
const svc_wifi_status_t *svc_wifi_status(void);

/** @brief Start an async scan. WATCH_EV_WIFI_SCAN_DONE follows. */
esp_err_t svc_wifi_scan_start(void);

/**
 * @brief Read the last scan, strongest first.
 *
 * @param[out] out    Array of at least @p max entries.
 * @return Number written.
 */
size_t svc_wifi_get_scan(svc_wifi_ap_t *out, size_t max);

/** @brief Connect and, on success, save the credentials. */
esp_err_t svc_wifi_connect(const char *ssid, const char *password);

/** @brief Reconnect to the saved network. ESP_ERR_NOT_FOUND if there is none. */
esp_err_t svc_wifi_connect_saved(void);

/** @brief Drop the link but leave the radio on. */
esp_err_t svc_wifi_disconnect(void);

/** @brief Forget the saved credentials and disconnect. */
esp_err_t svc_wifi_forget(void);

#ifdef __cplusplus
}
#endif
