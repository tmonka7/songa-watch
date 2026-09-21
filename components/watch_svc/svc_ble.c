#include "watch_svc/svc_ble.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_settings.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"

static const char *TAG = "svc_ble";

/* Serial-over-GATT profiles used by the BLE OBD2 adapters on the market.
 * Tried in order; the first service present on the peer wins. */
typedef struct {
    ble_uuid16_t service;
    ble_uuid16_t write_chr;
    ble_uuid16_t notify_chr;
    const char  *name;
} serial_profile_t;

static const serial_profile_t k_profiles[] = {
    { BLE_UUID16_INIT(0xFFF0), BLE_UUID16_INIT(0xFFF2), BLE_UUID16_INIT(0xFFF1), "FFF0" },
    { BLE_UUID16_INIT(0xFFE0), BLE_UUID16_INIT(0xFFE1), BLE_UUID16_INIT(0xFFE1), "FFE0" },
    { BLE_UUID16_INIT(0x18F0), BLE_UUID16_INIT(0x2AF1), BLE_UUID16_INIT(0x2AF0), "18F0" },
};
#define PROFILE_COUNT (sizeof(k_profiles) / sizeof(k_profiles[0]))

/* Names that mark a device as a likely ELM327 adapter, used to sort the
 * scan list and to auto-pick in svc_obd2_connect(). */
static const char *const k_obd2_names[] = {
    "OBDBLE", "IOS-Vlink", "VEEPEAK", "OBDII", "ELM327", "Android-Vlink", "V-LINK",
};
#define OBD2_NAME_COUNT (sizeof(k_obd2_names) / sizeof(k_obd2_names[0]))

static svc_ble_status_t     s_status;
static svc_ble_device_t     s_scan[SVC_BLE_MAX_SCAN];
static size_t               s_scan_count;
static SemaphoreHandle_t    s_scan_mutex;
static bool                 s_inited;
static bool                 s_host_synced;

/* 0 is a perfectly valid connection handle, so "no connection" needs the
 * dedicated sentinel rather than zero. */
#define CONN_NONE BLE_HS_CONN_HANDLE_NONE
static uint16_t             s_conn = CONN_NONE;

static uint16_t             s_write_handle;
static uint16_t             s_notify_handle;
static uint16_t             s_cccd_handle;
static bool                 s_write_no_rsp;
static const serial_profile_t *s_profile;

static svc_ble_notify_cb_t  s_notify_cb;
static void                *s_notify_arg;

/* Discovery runs asynchronously; connect() waits on this. */
static SemaphoreHandle_t    s_discovery_done;
static bool                 s_discovery_ok;

static int gap_event_cb(struct ble_gap_event *event, void *arg);

/* ------------------------------------------------------------------ helpers */

void svc_ble_format_addr(const uint8_t addr[6], char *buf, size_t len)
{
    if (buf == NULL || len < 18) {
        return;
    }
    /* NimBLE keeps addresses little-endian; print them the way they are
     * written on the back of a dongle. */
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

/* Case-insensitive substring search. Written out rather than using
 * strcasestr(), which is a GNU extension and not dependable across the
 * newlib builds ESP-IDF ships. */
static bool contains_ci(const char *haystack, const char *needle)
{
    const size_t nlen = strlen(needle);
    if (nlen == 0) {
        return true;
    }
    for (const char *p = haystack; *p != '\0'; p++) {
        if (strncasecmp(p, needle, nlen) == 0) {
            return true;
        }
    }
    return false;
}

static bool name_looks_like_obd2(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < OBD2_NAME_COUNT; i++) {
        if (contains_ci(name, k_obd2_names[i])) {
            return true;
        }
    }
    return false;
}

static void publish_status(void)
{
    svc_event_post(WATCH_EV_BLE_STATE, &s_status, sizeof(s_status));
}

static void set_state(svc_ble_state_t st)
{
    if (s_status.state == st) {
        return;
    }
    s_status.state = st;
    publish_status();
}

/* ------------------------------------------------------------ scan results */

static void scan_add(const struct ble_gap_disc_desc *desc)
{
    char name[SVC_BLE_NAME_MAX] = { 0 };
    struct ble_hs_adv_fields fields;

    if (ble_hs_adv_parse_fields(&fields, desc->data, desc->length_data) == 0) {
        if (fields.name != NULL && fields.name_len > 0) {
            const size_t n = (fields.name_len < SVC_BLE_NAME_MAX - 1)
                             ? fields.name_len : SVC_BLE_NAME_MAX - 1;
            memcpy(name, fields.name, n);
            name[n] = '\0';
        }
    }

    xSemaphoreTake(s_scan_mutex, portMAX_DELAY);

    /* Update in place if we have seen this address already - adverts repeat
     * several times a second and the list would otherwise fill with dupes. */
    for (size_t i = 0; i < s_scan_count; i++) {
        if (memcmp(s_scan[i].addr, desc->addr.val, 6) == 0) {
            s_scan[i].rssi = desc->rssi;
            if (name[0] != '\0') {
                strncpy(s_scan[i].name, name, SVC_BLE_NAME_MAX - 1);
                s_scan[i].looks_like_obd2 = name_looks_like_obd2(name);
            }
            xSemaphoreGive(s_scan_mutex);
            return;
        }
    }

    if (s_scan_count < SVC_BLE_MAX_SCAN) {
        svc_ble_device_t *d = &s_scan[s_scan_count++];
        memcpy(d->addr, desc->addr.val, 6);
        d->addr_type = desc->addr.type;
        d->rssi = desc->rssi;
        strncpy(d->name, name, SVC_BLE_NAME_MAX - 1);
        d->name[SVC_BLE_NAME_MAX - 1] = '\0';
        d->looks_like_obd2 = name_looks_like_obd2(name);
    }

    xSemaphoreGive(s_scan_mutex);
}

static void scan_sort_by_rssi(void)
{
    xSemaphoreTake(s_scan_mutex, portMAX_DELAY);
    for (size_t i = 1; i < s_scan_count; i++) {
        const svc_ble_device_t key = s_scan[i];
        size_t j = i;
        while (j > 0 && s_scan[j - 1].rssi < key.rssi) {
            s_scan[j] = s_scan[j - 1];
            j--;
        }
        s_scan[j] = key;
    }
    xSemaphoreGive(s_scan_mutex);
}

/* --------------------------------------------------------- GATT discovery */

static int on_cccd_written(uint16_t conn_handle, const struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle; (void)attr; (void)arg;

    s_discovery_ok = (error == NULL || error->status == 0);
    if (!s_discovery_ok) {
        ESP_LOGE(TAG, "could not subscribe to notifications (status %d)",
                 (error != NULL) ? error->status : -1);
    } else {
        ESP_LOGI(TAG, "subscribed, serial link ready (%s profile)",
                 (s_profile != NULL) ? s_profile->name : "?");
    }
    xSemaphoreGive(s_discovery_done);
    return 0;
}

static int on_dsc_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                       uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)arg;

    if (error != NULL && error->status != 0 && error->status != BLE_HS_EDONE) {
        s_discovery_ok = false;
        xSemaphoreGive(s_discovery_done);
        return 0;
    }

    /* ble_uuid_u16() is only meaningful on a 16-bit UUID; on a 128-bit one
     * it reads the wrong bytes, so the type has to be checked first. */
    if (dsc != NULL && dsc->uuid.u.type == BLE_UUID_TYPE_16 &&
        ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16 &&
        chr_val_handle == s_notify_handle && s_cccd_handle == 0) {
        s_cccd_handle = dsc->handle;
    }

    if (error != NULL && error->status == BLE_HS_EDONE) {
        if (s_cccd_handle == 0) {
            ESP_LOGE(TAG, "notify characteristic has no client-config descriptor");
            s_discovery_ok = false;
            xSemaphoreGive(s_discovery_done);
            return 0;
        }
        /* Enable notifications by writing 0x0001 to the CCCD. */
        static const uint8_t enable[2] = { 0x01, 0x00 };
        if (ble_gattc_write_flat(conn_handle, s_cccd_handle, enable, sizeof(enable),
                                 on_cccd_written, NULL) != 0) {
            s_discovery_ok = false;
            xSemaphoreGive(s_discovery_done);
        }
    }
    return 0;
}

static int on_chr_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;

    if (error != NULL && error->status != 0 && error->status != BLE_HS_EDONE) {
        s_discovery_ok = false;
        xSemaphoreGive(s_discovery_done);
        return 0;
    }

    if (chr != NULL && s_profile != NULL && chr->uuid.u.type == BLE_UUID_TYPE_16) {
        const uint16_t uuid = ble_uuid_u16(&chr->uuid.u);

        if (uuid == s_profile->write_chr.value &&
            (chr->properties & (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_WRITE_NO_RSP))) {
            s_write_handle = chr->val_handle;
            /* Prefer write-without-response: ELM327 traffic is chatty and
             * the round trip per command adds up badly. */
            s_write_no_rsp = (chr->properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP) != 0;
        }
        if (uuid == s_profile->notify_chr.value &&
            (chr->properties & (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE))) {
            s_notify_handle = chr->val_handle;
        }
    }

    if (error != NULL && error->status == BLE_HS_EDONE) {
        if (s_write_handle == 0 || s_notify_handle == 0) {
            ESP_LOGE(TAG, "peer has no usable write/notify pair");
            s_discovery_ok = false;
            xSemaphoreGive(s_discovery_done);
            return 0;
        }
        if (ble_gattc_disc_all_dscs(conn_handle, s_notify_handle, 0xFFFF,
                                    on_dsc_disc, NULL) != 0) {
            s_discovery_ok = false;
            xSemaphoreGive(s_discovery_done);
        }
    }
    return 0;
}

static int on_svc_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_svc *service, void *arg)
{
    (void)arg;

    if (error != NULL && error->status != 0 && error->status != BLE_HS_EDONE) {
        s_discovery_ok = false;
        xSemaphoreGive(s_discovery_done);
        return 0;
    }

    if (service != NULL && s_profile == NULL && service->uuid.u.type == BLE_UUID_TYPE_16) {
        const uint16_t uuid = ble_uuid_u16(&service->uuid.u);
        for (size_t i = 0; i < PROFILE_COUNT; i++) {
            if (uuid == k_profiles[i].service.value) {
                s_profile = &k_profiles[i];
                ESP_LOGI(TAG, "matched serial profile %s", s_profile->name);
                break;
            }
        }
    }

    if (error != NULL && error->status == BLE_HS_EDONE) {
        if (s_profile == NULL) {
            ESP_LOGE(TAG, "peer exposes no known serial service");
            s_discovery_ok = false;
            xSemaphoreGive(s_discovery_done);
            return 0;
        }
        if (ble_gattc_disc_all_chrs(conn_handle, 1, 0xFFFF, on_chr_disc, NULL) != 0) {
            s_discovery_ok = false;
            xSemaphoreGive(s_discovery_done);
        }
    }
    return 0;
}

/* ------------------------------------------------------------- GAP events */

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        scan_add(&event->disc);
        break;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        scan_sort_by_rssi();
        ESP_LOGI(TAG, "scan done, %u devices", (unsigned)s_scan_count);
        set_state((s_conn != CONN_NONE) ? SVC_BLE_CONNECTED : SVC_BLE_IDLE);
        svc_event_post(WATCH_EV_BLE_SCAN_DONE, NULL, 0);
        break;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            s_status.conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected, discovering services");
            set_state(SVC_BLE_CONNECTED);

            s_profile = NULL;
            s_write_handle = s_notify_handle = s_cccd_handle = 0;
            s_discovery_ok = false;
            if (ble_gattc_disc_all_svcs(event->connect.conn_handle, on_svc_disc, NULL) != 0) {
                xSemaphoreGive(s_discovery_done);
            }
        } else {
            ESP_LOGW(TAG, "connect failed (status %d)", event->connect.status);
            s_conn = CONN_NONE;
            s_status.conn_handle = CONN_NONE;
            set_state(SVC_BLE_IDLE);
            xSemaphoreGive(s_discovery_done);
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "disconnected (reason %d)", event->disconnect.reason);
        s_conn = CONN_NONE;
        s_status.conn_handle = CONN_NONE;
        s_write_handle = s_notify_handle = s_cccd_handle = 0;
        s_profile = NULL;
        memset(&s_status.peer, 0, sizeof(s_status.peer));
        set_state(SVC_BLE_IDLE);
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (s_notify_cb != NULL && event->notify_rx.om != NULL) {
            /* Flatten the mbuf chain; callers want one contiguous buffer. */
            const uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
            uint8_t stack_buf[256];
            if (len <= sizeof(stack_buf)) {
                if (ble_hs_mbuf_to_flat(event->notify_rx.om, stack_buf,
                                        sizeof(stack_buf), NULL) == 0) {
                    s_notify_cb(stack_buf, len, s_notify_arg);
                }
            } else {
                ESP_LOGW(TAG, "notification of %u bytes dropped (too large)", len);
            }
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU now %d", event->mtu.value);
        break;

    default:
        break;
    }
    return 0;
}

/* --------------------------------------------------------------- host task */

static void on_host_sync(void)
{
    (void)ble_hs_util_ensure_addr(0);
    s_host_synced = true;
    ESP_LOGI(TAG, "BLE host synced");
    set_state(SVC_BLE_IDLE);
}

static void on_host_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason %d", reason);
    s_host_synced = false;
    s_conn = CONN_NONE;
    s_status.conn_handle = CONN_NONE;
    set_state(SVC_BLE_OFF);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();              /* returns only when the port is stopped */
    nimble_port_freertos_deinit();
}

/* ---------------------------------------------------------------------- api */

esp_err_t svc_ble_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }
    s_scan_mutex = xSemaphoreCreateMutex();
    s_discovery_done = xSemaphoreCreateBinary();
    if (s_scan_mutex == NULL || s_discovery_done == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_status.state = SVC_BLE_OFF;
    s_status.conn_handle = CONN_NONE;
    s_conn = CONN_NONE;
    s_inited = true;
    return ESP_OK;
}

esp_err_t svc_ble_set_enabled(bool enable)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        if (s_status.state != SVC_BLE_OFF) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "nimble init");
        ble_hs_cfg.sync_cb  = on_host_sync;
        ble_hs_cfg.reset_cb = on_host_reset;
        nimble_port_freertos_init(host_task);

        /* The host comes up asynchronously; give it a moment so callers can
         * scan straight after enabling. */
        for (int i = 0; i < 50 && !s_host_synced; i++) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (!s_host_synced) {
            ESP_LOGW(TAG, "BLE host did not sync in time");
        }
    } else {
        if (s_status.state == SVC_BLE_OFF) {
            return ESP_OK;
        }
        (void)svc_ble_disconnect();
        (void)ble_gap_disc_cancel();
        nimble_port_stop();
        nimble_port_deinit();
        s_host_synced = false;
        set_state(SVC_BLE_OFF);
    }
    return svc_settings_set_ble_enable(enable);
}

const svc_ble_status_t *svc_ble_status(void)
{
    return &s_status;
}

esp_err_t svc_ble_scan_start(uint32_t duration_ms)
{
    if (!s_host_synced) {
        ESP_RETURN_ON_ERROR(svc_ble_set_enabled(true), TAG, "enable for scan");
        if (!s_host_synced) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    xSemaphoreTake(s_scan_mutex, portMAX_DELAY);
    s_scan_count = 0;
    xSemaphoreGive(s_scan_mutex);

    uint8_t own_addr_type = 0;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        return ESP_FAIL;
    }

    const struct ble_gap_disc_params params = {
        .itvl = 0,              /* controller defaults */
        .window = 0,
        .filter_policy = 0,
        .limited = 0,
        .passive = 0,           /* active: we want scan responses for names */
        .filter_duplicates = 0, /* we de-duplicate in scan_add() by address */
    };

    const int rc = ble_gap_disc(own_addr_type, (int32_t)duration_ms, &params,
                                gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        return ESP_FAIL;
    }
    set_state(SVC_BLE_SCANNING);
    return ESP_OK;
}

esp_err_t svc_ble_scan_stop(void)
{
    if (s_status.state != SVC_BLE_SCANNING) {
        return ESP_OK;
    }
    ble_gap_disc_cancel();
    scan_sort_by_rssi();
    set_state(SVC_BLE_IDLE);
    svc_event_post(WATCH_EV_BLE_SCAN_DONE, NULL, 0);
    return ESP_OK;
}

size_t svc_ble_get_scan(svc_ble_device_t *out, size_t max)
{
    if (out == NULL || s_scan_mutex == NULL) {
        return 0;
    }
    xSemaphoreTake(s_scan_mutex, portMAX_DELAY);
    const size_t n = (s_scan_count < max) ? s_scan_count : max;
    memcpy(out, s_scan, n * sizeof(svc_ble_device_t));
    xSemaphoreGive(s_scan_mutex);
    return n;
}

esp_err_t svc_ble_connect(const uint8_t addr[6], uint8_t addr_type)
{
    if (addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_host_synced) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_status.state == SVC_BLE_SCANNING) {
        ble_gap_disc_cancel();
    }

    ble_addr_t peer = { .type = addr_type };
    memcpy(peer.val, addr, 6);

    uint8_t own_addr_type = 0;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        return ESP_FAIL;
    }

    set_state(SVC_BLE_CONNECTING);
    /* Drain any stale completion before arming the wait. */
    xSemaphoreTake(s_discovery_done, 0);

    const int rc = ble_gap_connect(own_addr_type, &peer, 10000, NULL, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_connect failed: %d", rc);
        set_state(SVC_BLE_IDLE);
        return ESP_FAIL;
    }

    /* Connect + full service discovery + CCCD write. 15 s is generous; a
     * dongle that has not finished by then is not going to. */
    if (xSemaphoreTake(s_discovery_done, pdMS_TO_TICKS(15000)) != pdTRUE) {
        ESP_LOGE(TAG, "service discovery timed out");
        (void)svc_ble_disconnect();
        return ESP_ERR_TIMEOUT;
    }
    if (!s_discovery_ok) {
        (void)svc_ble_disconnect();
        return ESP_ERR_NOT_SUPPORTED;
    }

    memcpy(s_status.peer.addr, addr, 6);
    s_status.peer.addr_type = addr_type;
    /* Carry over the advertised name if we saw it during the scan. */
    xSemaphoreTake(s_scan_mutex, portMAX_DELAY);
    for (size_t i = 0; i < s_scan_count; i++) {
        if (memcmp(s_scan[i].addr, addr, 6) == 0) {
            strncpy(s_status.peer.name, s_scan[i].name, SVC_BLE_NAME_MAX - 1);
            s_status.peer.looks_like_obd2 = s_scan[i].looks_like_obd2;
            break;
        }
    }
    xSemaphoreGive(s_scan_mutex);

    publish_status();
    return ESP_OK;
}

esp_err_t svc_ble_disconnect(void)
{
    if (s_conn == CONN_NONE) {
        return ESP_OK;
    }
    ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    return ESP_OK;
}

esp_err_t svc_ble_write(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_conn == CONN_NONE || s_write_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Leave room for the ATT opcode and handle in each PDU. */
    uint16_t mtu = ble_att_mtu(s_conn);
    if (mtu < 23) { mtu = 23; }
    const uint16_t chunk_max = mtu - 3;

    uint16_t sent = 0;
    while (sent < len) {
        const uint16_t chunk = ((len - sent) > chunk_max) ? chunk_max : (len - sent);
        const int rc = s_write_no_rsp
            ? ble_gattc_write_no_rsp_flat(s_conn, s_write_handle,
                                          data + sent, chunk)
            : ble_gattc_write_flat(s_conn, s_write_handle,
                                   data + sent, chunk, NULL, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "GATT write failed: %d", rc);
            return ESP_FAIL;
        }
        sent += chunk;
    }
    return ESP_OK;
}

esp_err_t svc_ble_set_notify_cb(svc_ble_notify_cb_t cb, void *arg)
{
    s_notify_cb = cb;
    s_notify_arg = arg;
    return ESP_OK;
}
