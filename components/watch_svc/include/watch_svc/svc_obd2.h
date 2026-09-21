/*
 * OBD2 over a BLE ELM327 adapter.
 *
 * The ESP32-S3 has no Bluetooth Classic, so the classic blue ELM327 dongles
 * and the "OBDII Interface" boxes cannot be used with this board at all.
 * What works is a BLE adapter that exposes a Nordic-UART-style write/notify
 * pair - the ones that advertise as OBDBLE, IOS-Vlink, VEEPEAK and similar.
 *
 * The protocol is ELM327 AT commands and SAE J1979 mode/PID requests as
 * ASCII hex, terminated by '>' when the adapter wants the next command.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_OBD2_MAX_DTC   16
#define SVC_OBD2_VIN_LEN   18

typedef enum {
    SVC_OBD2_OFF = 0,
    SVC_OBD2_SEARCHING,     /* scanning for an adapter */
    SVC_OBD2_CONNECTING,    /* link up, running the AT init sequence */
    SVC_OBD2_LINK_READY,    /* adapter answers, no vehicle protocol yet */
    SVC_OBD2_CONNECTED,     /* talking to the ECU */
    SVC_OBD2_ERROR,
} svc_obd2_state_t;

typedef struct {
    svc_obd2_state_t state;
    char             adapter_name[32];
    char             protocol[24];   /* what ATDP reported */
    char             vin[SVC_OBD2_VIN_LEN + 1];
    bool             mil_on;         /* dashboard warning light */
    uint8_t          dtc_count;
    char             last_error[48];
} svc_obd2_status_t;

/* A live PID reading. `valid` is false when the ECU does not support it -
 * which is normal and common, so screens must check it. */
typedef struct {
    bool  valid;
    float value;
} svc_obd2_pid_t;

typedef struct {
    svc_obd2_pid_t engine_load;      /* %     PID 0x04 */
    svc_obd2_pid_t coolant_temp;     /* deg C PID 0x05 */
    svc_obd2_pid_t rpm;              /* rpm   PID 0x0C */
    svc_obd2_pid_t speed;            /* km/h  PID 0x0D */
    svc_obd2_pid_t intake_temp;      /* deg C PID 0x0F */
    svc_obd2_pid_t throttle;         /* %     PID 0x11 */
    svc_obd2_pid_t fuel_level;       /* %     PID 0x2F */
    svc_obd2_pid_t battery_voltage;  /* V, from the adapter's ATRV */
    int64_t        timestamp_us;
} svc_obd2_live_t;

typedef struct {
    char code[6];        /* "P0301" */
    bool pending;        /* mode 07 rather than mode 03 */
} svc_obd2_dtc_t;

/* Readiness monitors, from mode 01 PID 0x01. */
typedef enum {
    SVC_OBD2_MON_OK = 0,
    SVC_OBD2_MON_INCOMPLETE,
    SVC_OBD2_MON_FAULT,
    SVC_OBD2_MON_UNSUPPORTED,
} svc_obd2_monitor_t;

typedef struct {
    svc_obd2_monitor_t misfire;
    svc_obd2_monitor_t fuel_system;
    svc_obd2_monitor_t components;
    svc_obd2_monitor_t catalyst;
    svc_obd2_monitor_t oxygen_sensor;
    svc_obd2_monitor_t egr_system;
} svc_obd2_monitors_t;

/** @brief Start the OBD2 service. Needs svc_ble_init() first. */
esp_err_t svc_obd2_init(void);

/** @brief Current link and vehicle state. */
const svc_obd2_status_t *svc_obd2_status(void);

/**
 * @brief Look for an adapter and connect.
 *
 * Reuses the adapter saved by svc_settings when there is one, otherwise
 * scans and picks the strongest device that looks like an ELM327.
 */
esp_err_t svc_obd2_connect(void);

/** @brief Connect to one specific adapter. */
esp_err_t svc_obd2_connect_addr(const uint8_t addr[6], uint8_t addr_type);

/** @brief Drop the adapter link. */
esp_err_t svc_obd2_disconnect(void);

/**
 * @brief Start or stop the live-data poll loop.
 *
 * While running, WATCH_EV_OBD2_LIVE arrives about twice a second. Stop it
 * when the Live Data screen closes - it keeps the BLE link busy.
 */
esp_err_t svc_obd2_set_polling(bool enable);

/** @brief The most recent live readings. */
const svc_obd2_live_t *svc_obd2_live(void);

/** @brief Re-read stored and pending trouble codes. WATCH_EV_OBD2_DTC follows. */
esp_err_t svc_obd2_read_dtcs(void);

/** @brief Copy the last DTC read. @return entries written. */
size_t svc_obd2_get_dtcs(svc_obd2_dtc_t *out, size_t max);

/**
 * @brief Clear trouble codes and turn the warning light off (mode 04).
 *
 * This also wipes the readiness monitors, which can take a long drive cycle
 * to re-run - worth telling the user before calling it.
 */
esp_err_t svc_obd2_clear_dtcs(void);

/** @brief Read the freeze frame stored against @p frame (usually 0). */
esp_err_t svc_obd2_read_freeze_frame(uint8_t frame, svc_obd2_live_t *out);

/** @brief Read the readiness monitors. */
esp_err_t svc_obd2_read_monitors(svc_obd2_monitors_t *out);

/** @brief A human-readable description of a DTC, or NULL if unknown. */
const char *svc_obd2_describe_dtc(const char *code);

#ifdef __cplusplus
}
#endif
