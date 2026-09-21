#include "watch_svc/svc_wifi.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_settings.h"
#include "watch_svc/svc_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "svc_wifi";

#define RECONNECT_MAX_TRIES 4

static svc_wifi_status_t s_status;
static svc_wifi_ap_t     s_scan[SVC_WIFI_MAX_SCAN];
static size_t            s_scan_count;
static esp_netif_t      *s_netif;
static bool              s_inited;
static bool              s_want_connection;
static int               s_retries;

/* RSSI to a 0-4 bar count, matching what the signal icon draws. */
static uint8_t rssi_to_bars(int8_t rssi)
{
    if (rssi >= -55) { return 4; }
    if (rssi >= -66) { return 3; }
    if (rssi >= -77) { return 2; }
    if (rssi >= -88) { return 1; }
    return 0;
}

static void publish(void)
{
    svc_event_post(WATCH_EV_WIFI_STATE, &s_status, sizeof(s_status));
}

static void set_state(svc_wifi_state_t st)
{
    if (s_status.state == st) {
        return;
    }
    s_status.state = st;
    publish();
}

/* --------------------------------------------------------- event handlers */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;

    switch (id) {
    case WIFI_EVENT_STA_START:
        if (s_want_connection) {
            esp_wifi_connect();
        }
        break;

    case WIFI_EVENT_STA_CONNECTED: {
        const wifi_event_sta_connected_t *e = (const wifi_event_sta_connected_t *)data;
        s_retries = 0;
        memcpy(s_status.ssid, e->ssid, (e->ssid_len < WATCH_SSID_MAX_LEN)
                                        ? e->ssid_len : WATCH_SSID_MAX_LEN - 1);
        s_status.ssid[WATCH_SSID_MAX_LEN - 1] = '\0';
        ESP_LOGI(TAG, "associated with %s, waiting for an address", s_status.ssid);
        break;
    }

    case WIFI_EVENT_STA_DISCONNECTED: {
        s_status.ip[0] = '\0';
        s_status.rssi = 0;
        s_status.bars = 0;

        if (s_want_connection && s_retries < RECONNECT_MAX_TRIES) {
            s_retries++;
            ESP_LOGW(TAG, "disconnected, retry %d/%d", s_retries, RECONNECT_MAX_TRIES);
            set_state(SVC_WIFI_CONNECTING);
            esp_wifi_connect();
        } else {
            /* Give up rather than retry forever - a station stuck in a
             * connect loop is one of the fastest ways to flatten the cell. */
            if (s_want_connection) {
                ESP_LOGW(TAG, "giving up after %d attempts", s_retries);
                s_want_connection = false;
                set_state(SVC_WIFI_FAILED);
            } else {
                set_state(SVC_WIFI_DISCONNECTED);
            }
        }
        break;
    }

    case WIFI_EVENT_SCAN_DONE: {
        uint16_t found = 0;
        esp_wifi_scan_get_ap_num(&found);
        if (found > SVC_WIFI_MAX_SCAN) {
            found = SVC_WIFI_MAX_SCAN;
        }

        wifi_ap_record_t *recs = calloc(found ? found : 1, sizeof(wifi_ap_record_t));
        s_scan_count = 0;
        if (recs != NULL) {
            uint16_t n = found;
            if (esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK) {
                for (uint16_t i = 0; i < n && s_scan_count < SVC_WIFI_MAX_SCAN; i++) {
                    if (recs[i].ssid[0] == '\0') {
                        continue;   /* hidden network, nothing to show */
                    }
                    svc_wifi_ap_t *ap = &s_scan[s_scan_count++];
                    strncpy(ap->ssid, (const char *)recs[i].ssid, WATCH_SSID_MAX_LEN - 1);
                    ap->ssid[WATCH_SSID_MAX_LEN - 1] = '\0';
                    ap->rssi    = recs[i].rssi;
                    ap->bars    = rssi_to_bars(recs[i].rssi);
                    ap->secured = (recs[i].authmode != WIFI_AUTH_OPEN);
                }
            }
            free(recs);
        }
        /* esp_wifi returns records strongest first already, so no sort here. */
        s_status.scanning = false;
        ESP_LOGI(TAG, "scan found %u networks", (unsigned)s_scan_count);
        svc_event_post(WATCH_EV_WIFI_SCAN_DONE, NULL, 0);
        publish();
        break;
    }

    default:
        break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;

    if (id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)data;
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&e->ip_info.ip));

        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s_status.rssi = ap.rssi;
            s_status.bars = rssi_to_bars(ap.rssi);
        }
        ESP_LOGI(TAG, "connected, address %s", s_status.ip);
        set_state(SVC_WIFI_CONNECTED);

        if (svc_settings_get()->ntp_enable) {
            svc_time_sync_now();
        }
    }
}

/* ---------------------------------------------------------------------- api */

esp_err_t svc_wifi_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    /* The default event loop is separate from the watch loop; ESP-IDF's
     * Wi-Fi driver insists on posting to this one. */
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_netif = esp_netif_create_default_wifi_sta();
    if (s_netif == NULL) {
        return ESP_FAIL;
    }

    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL), TAG, "wifi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL, NULL), TAG, "ip handler");

    /* Modem sleep between DTIM beacons. Without this a connected station
     * costs tens of milliamps continuously. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_MIN_MODEM), TAG, "wifi ps");

    s_status.state = SVC_WIFI_OFF;
    s_inited = true;
    ESP_LOGI(TAG, "Wi-Fi ready (radio off)");
    return ESP_OK;
}

esp_err_t svc_wifi_set_enabled(bool enable)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        if (s_status.state != SVC_WIFI_OFF) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
        set_state(SVC_WIFI_DISCONNECTED);
    } else {
        if (s_status.state == SVC_WIFI_OFF) {
            return ESP_OK;
        }
        s_want_connection = false;
        /* Stopping the driver, not just disconnecting: an idle-but-started
         * station keeps the radio clocked. */
        ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "wifi stop");
        s_status.ip[0] = '\0';
        s_status.ssid[0] = '\0';
        s_status.bars = 0;
        s_status.scanning = false;
        set_state(SVC_WIFI_OFF);
    }
    return svc_settings_set_wifi_enable(enable);
}

const svc_wifi_status_t *svc_wifi_status(void)
{
    return &s_status;
}

esp_err_t svc_wifi_scan_start(void)
{
    if (s_status.state == SVC_WIFI_OFF) {
        ESP_RETURN_ON_ERROR(svc_wifi_set_enabled(true), TAG, "enable for scan");
    }
    if (s_status.scanning) {
        return ESP_OK;
    }

    const wifi_scan_config_t cfg = {
        .show_hidden = false,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = { .min = 100, .max = 200 },
    };
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&cfg, false), TAG, "scan start");
    s_status.scanning = true;
    publish();
    return ESP_OK;
}

size_t svc_wifi_get_scan(svc_wifi_ap_t *out, size_t max)
{
    if (out == NULL) {
        return 0;
    }
    const size_t n = (s_scan_count < max) ? s_scan_count : max;
    memcpy(out, s_scan, n * sizeof(svc_wifi_ap_t));
    return n;
}

esp_err_t svc_wifi_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_status.state == SVC_WIFI_OFF) {
        ESP_RETURN_ON_ERROR(svc_wifi_set_enabled(true), TAG, "enable for connect");
    }

    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    }
    cfg.sta.threshold.authmode = (password != NULL && password[0] != '\0')
                                 ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "set config");

    s_want_connection = true;
    s_retries = 0;
    set_state(SVC_WIFI_CONNECTING);

    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        s_want_connection = false;
        set_state(SVC_WIFI_FAILED);
        return err;
    }

    /* Only remember credentials that got us as far as an association
     * attempt; the handler saves nothing on failure. */
    (void)svc_settings_save_wifi(ssid, password);
    return ESP_OK;
}

esp_err_t svc_wifi_connect_saved(void)
{
    char ssid[WATCH_SSID_MAX_LEN] = { 0 };
    char pass[WATCH_PASS_MAX_LEN] = { 0 };
    ESP_RETURN_ON_ERROR(svc_settings_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass)),
                        TAG, "no saved network");
    if (ssid[0] == '\0') {
        /* svc_wifi_forget() blanks the record rather than deleting the key. */
        return ESP_ERR_NOT_FOUND;
    }
    return svc_wifi_connect(ssid, pass);
}

esp_err_t svc_wifi_disconnect(void)
{
    s_want_connection = false;
    const esp_err_t err = esp_wifi_disconnect();
    set_state(SVC_WIFI_DISCONNECTED);
    return err;
}

esp_err_t svc_wifi_forget(void)
{
    (void)svc_settings_save_wifi("", "");
    return svc_wifi_disconnect();
}
