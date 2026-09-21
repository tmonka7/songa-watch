/*
 * Bluetooth Low Energy, as a GATT central.
 *
 * The ESP32-S3 radio has no Bluetooth Classic, so everything here is BLE.
 * That decides the OBD2 story too: a classic ELM327 dongle cannot pair with
 * this chip at all, and only the BLE variants work.
 *
 * This layer owns the NimBLE host, scanning and the connection. Profile
 * logic on top of a connection lives elsewhere - svc_obd2 is the one
 * consumer today.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_BLE_MAX_SCAN     16
#define SVC_BLE_NAME_MAX     32

typedef enum {
    SVC_BLE_OFF = 0,
    SVC_BLE_IDLE,
    SVC_BLE_SCANNING,
    SVC_BLE_CONNECTING,
    SVC_BLE_CONNECTED,
} svc_ble_state_t;

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    char    name[SVC_BLE_NAME_MAX];
    int8_t  rssi;
    bool    looks_like_obd2;   /* name or service UUID matches a known dongle */
} svc_ble_device_t;

typedef struct {
    svc_ble_state_t  state;
    svc_ble_device_t peer;     /* valid while CONNECTED */
    uint16_t         conn_handle;
} svc_ble_status_t;

/**
 * @brief Called for every notification on the subscribed characteristic.
 *
 * Runs on the NimBLE host task. Do not block and do not touch LVGL.
 */
typedef void (*svc_ble_notify_cb_t)(const uint8_t *data, uint16_t len, void *arg);

/** @brief Bring up the NimBLE host. Does not start the radio advertising or scanning. */
esp_err_t svc_ble_init(void);

/** @brief Enable or disable the controller. Off releases the radio entirely. */
esp_err_t svc_ble_set_enabled(bool enable);

/** @brief Current state. */
const svc_ble_status_t *svc_ble_status(void);

/** @brief Start a scan of @p duration_ms. WATCH_EV_BLE_SCAN_DONE follows. */
esp_err_t svc_ble_scan_start(uint32_t duration_ms);

/** @brief Stop a scan early. */
esp_err_t svc_ble_scan_stop(void);

/** @brief Read the last scan, strongest first. @return entries written. */
size_t svc_ble_get_scan(svc_ble_device_t *out, size_t max);

/**
 * @brief Connect and discover services.
 *
 * On success the state becomes SVC_BLE_CONNECTED and a serial-style
 * write/notify pair has been resolved, if the peer exposes one.
 */
esp_err_t svc_ble_connect(const uint8_t addr[6], uint8_t addr_type);

/** @brief Drop the link. */
esp_err_t svc_ble_disconnect(void);

/**
 * @brief Write to the peer's serial write characteristic.
 *
 * Splits @p len across as many writes as the negotiated MTU needs.
 */
esp_err_t svc_ble_write(const uint8_t *data, uint16_t len);

/** @brief Route notifications from the peer's serial notify characteristic. */
esp_err_t svc_ble_set_notify_cb(svc_ble_notify_cb_t cb, void *arg);

/** @brief Format an address as "AA:BB:CC:DD:EE:FF". @p len >= 18. */
void svc_ble_format_addr(const uint8_t addr[6], char *buf, size_t len);

#ifdef __cplusplus
}
#endif
