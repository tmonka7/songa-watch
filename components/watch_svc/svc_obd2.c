#include "watch_svc/svc_obd2.h"
#include "watch_svc/svc_ble.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "svc_obd2";

#define RX_BUF_SIZE        512
#define CMD_TIMEOUT_MS    5000
#define RESET_TIMEOUT_MS  9000   /* ATZ can take a while on a cold adapter */
#define POLL_PERIOD_MS     500
#define SCAN_DURATION_MS  6000

static svc_obd2_status_t s_status;
static svc_obd2_live_t   s_live;
static svc_obd2_dtc_t    s_dtcs[SVC_OBD2_MAX_DTC];
static size_t            s_dtc_count;

static char              s_rx[RX_BUF_SIZE];
static volatile size_t   s_rx_len;
static SemaphoreHandle_t s_prompt_sem;    /* given when '>' arrives */
static SemaphoreHandle_t s_bus_mutex;     /* one command at a time */
static TaskHandle_t      s_poll_task;
static volatile bool     s_polling;

/* ------------------------------------------------------------------ helpers */

static void set_state(svc_obd2_state_t st)
{
    if (s_status.state == st) {
        return;
    }
    s_status.state = st;
    svc_event_post(WATCH_EV_OBD2_STATE, &s_status, sizeof(s_status));
}

static void set_error(const char *msg)
{
    strncpy(s_status.last_error, (msg != NULL) ? msg : "", sizeof(s_status.last_error) - 1);
    s_status.last_error[sizeof(s_status.last_error) - 1] = '\0';
}

/* Runs on the NimBLE host task: copy bytes out and signal the prompt, but
 * do no parsing here. */
static void on_ble_notify(const uint8_t *data, uint16_t len, void *arg)
{
    (void)arg;
    for (uint16_t i = 0; i < len; i++) {
        const char c = (char)data[i];
        if (s_rx_len < RX_BUF_SIZE - 1) {
            s_rx[s_rx_len++] = c;
        }
        if (c == '>') {
            /* ELM327 prints '>' when it is ready for the next command. */
            s_rx[s_rx_len] = '\0';
            xSemaphoreGive(s_prompt_sem);
        }
    }
}

/**
 * Send one command and wait for the adapter's prompt.
 *
 * @param out   Receives the reply with whitespace, echoes and the prompt
 *              stripped. May be NULL.
 */
static esp_err_t elm_cmd(const char *cmd, char *out, size_t out_len, uint32_t timeout_ms)
{
    if (cmd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(timeout_ms + 1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_rx_len = 0;
    s_rx[0] = '\0';
    xSemaphoreTake(s_prompt_sem, 0);   /* drop a stale prompt */

    char line[64];
    const int n = snprintf(line, sizeof(line), "%s\r", cmd);
    esp_err_t err = svc_ble_write((const uint8_t *)line, (uint16_t)n);
    if (err != ESP_OK) {
        xSemaphoreGive(s_bus_mutex);
        return err;
    }

    if (xSemaphoreTake(s_prompt_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "no reply to '%s'", cmd);
        xSemaphoreGive(s_bus_mutex);
        return ESP_ERR_TIMEOUT;
    }

    if (out != NULL && out_len > 0) {
        /* Strip CR/LF, spaces and the trailing prompt; drop the echoed
         * command if the adapter still has echo on. */
        size_t o = 0;
        for (size_t i = 0; i < s_rx_len && o < out_len - 1; i++) {
            const char c = s_rx[i];
            if (c == '\r' || c == '\n' || c == ' ' || c == '>') {
                continue;
            }
            out[o++] = c;
        }
        out[o] = '\0';

        const size_t cmd_len = strlen(cmd);
        if (o >= cmd_len && strncasecmp(out, cmd, cmd_len) == 0) {
            memmove(out, out + cmd_len, o - cmd_len + 1);
        }
    }

    xSemaphoreGive(s_bus_mutex);
    return ESP_OK;
}

/*
 * Only the replies that mean the request produced nothing.
 *
 * Deliberately NOT treated as errors: "SEARCHING..." and "BUS INIT: OK",
 * which the adapter prints *before* a perfectly good response on the first
 * request of a session. Rejecting those would make every cold connect look
 * like a dead ECU. Callers confirm success by finding the positive-response
 * marker, not by the absence of chatter.
 */
static bool reply_is_error(const char *s)
{
    return (s == NULL) || s[0] == '\0' ||
           strstr(s, "NODATA")       != NULL ||
           strstr(s, "UNABLE")       != NULL ||
           strstr(s, "STOPPED")      != NULL ||
           strstr(s, "CANERROR")     != NULL ||
           strstr(s, "BUSERROR")     != NULL ||
           strstr(s, "BUSINIT:ERROR")!= NULL ||
           strstr(s, "BUSBUSY")      != NULL ||
           strstr(s, "DATAERROR")    != NULL ||
           strstr(s, "?")            != NULL;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    return -1;
}

/** Convert an ASCII-hex string to bytes. @return bytes written, or -1. */
static int hex_to_bytes(const char *s, uint8_t *out, size_t max)
{
    size_t n = 0;
    while (s[0] != '\0' && s[1] != '\0' && n < max) {
        const int hi = hex_nibble(s[0]);
        const int lo = hex_nibble(s[1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[n++] = (uint8_t)((hi << 4) | lo);
        s += 2;
    }
    return (int)n;
}

/**
 * Run a mode 01 request and return the payload that follows the
 * "41 <pid>" echo.
 *
 * Many ECUs answer a single request from more than one controller, so the
 * reply can carry several "41 xx ..." blocks. We take the first.
 */
static int obd_query(uint8_t mode, uint8_t pid, uint8_t *payload, size_t max)
{
    char cmd[8];
    char resp[RX_BUF_SIZE];
    snprintf(cmd, sizeof(cmd), "%02X%02X", mode, pid);

    if (elm_cmd(cmd, resp, sizeof(resp), CMD_TIMEOUT_MS) != ESP_OK) {
        return -1;
    }
    if (reply_is_error(resp)) {
        return -1;
    }

    /* Find the positive-response marker: request mode + 0x40. */
    char marker[5];
    snprintf(marker, sizeof(marker), "%02X%02X", (unsigned)(mode + 0x40), pid);
    const char *p = strstr(resp, marker);
    if (p == NULL) {
        return -1;
    }
    p += strlen(marker);

    return hex_to_bytes(p, payload, max);
}

/* -------------------------------------------------------------- live data */

/* Declared up here because read_live_into() reaches for it through a macro. */
static int obd_query_frame(uint8_t pid, uint8_t frame, uint8_t *payload, size_t max);

static void set_pid(svc_obd2_pid_t *dst, bool ok, float value)
{
    dst->valid = ok;
    dst->value = ok ? value : 0.0f;
}

static void read_live_into(svc_obd2_live_t *dst, uint8_t mode, uint8_t frame)
{
    uint8_t d[8] = { 0 };
    int n;

    /* In freeze-frame mode (02) every request carries the frame number as
     * an extra byte, so those go through a slightly different path. */
    const bool freeze = (mode == 0x02);

    #define QUERY(pid) (freeze ? obd_query_frame(pid, frame, d, sizeof(d)) \
                               : obd_query(0x01, (pid), d, sizeof(d)))

    n = QUERY(0x04);
    set_pid(&dst->engine_load, n >= 1, (float)d[0] * 100.0f / 255.0f);

    n = QUERY(0x05);
    set_pid(&dst->coolant_temp, n >= 1, (float)d[0] - 40.0f);

    n = QUERY(0x0C);
    set_pid(&dst->rpm, n >= 2, ((float)d[0] * 256.0f + (float)d[1]) / 4.0f);

    n = QUERY(0x0D);
    set_pid(&dst->speed, n >= 1, (float)d[0]);

    n = QUERY(0x0F);
    set_pid(&dst->intake_temp, n >= 1, (float)d[0] - 40.0f);

    n = QUERY(0x11);
    set_pid(&dst->throttle, n >= 1, (float)d[0] * 100.0f / 255.0f);

    n = QUERY(0x2F);
    set_pid(&dst->fuel_level, n >= 1, (float)d[0] * 100.0f / 255.0f);

    #undef QUERY

    dst->timestamp_us = esp_timer_get_time();
}

static int obd_query_frame(uint8_t pid, uint8_t frame, uint8_t *payload, size_t max)
{
    char cmd[10];
    char resp[RX_BUF_SIZE];
    snprintf(cmd, sizeof(cmd), "02%02X%02X", pid, frame);

    if (elm_cmd(cmd, resp, sizeof(resp), CMD_TIMEOUT_MS) != ESP_OK ||
        reply_is_error(resp)) {
        return -1;
    }

    char marker[7];
    snprintf(marker, sizeof(marker), "42%02X%02X", pid, frame);
    const char *p = strstr(resp, marker);
    if (p == NULL) {
        return -1;
    }
    return hex_to_bytes(p + strlen(marker), payload, max);
}

static void read_battery_voltage(svc_obd2_live_t *dst)
{
    /* ATRV is the adapter's own reading of the vehicle's battery, not a
     * PID, so it works even before a protocol is negotiated. */
    char resp[32];
    if (elm_cmd("ATRV", resp, sizeof(resp), CMD_TIMEOUT_MS) != ESP_OK) {
        set_pid(&dst->battery_voltage, false, 0.0f);
        return;
    }
    const float v = strtof(resp, NULL);
    set_pid(&dst->battery_voltage, (v > 1.0f && v < 40.0f), v);
}

static void poll_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!s_polling || s_status.state != SVC_OBD2_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        svc_obd2_live_t fresh = { 0 };
        read_live_into(&fresh, 0x01, 0);
        read_battery_voltage(&fresh);
        s_live = fresh;
        svc_event_post(WATCH_EV_OBD2_LIVE, &s_live, sizeof(s_live));

        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

/* ------------------------------------------------------------------- DTCs */

/* Decode one 2-byte DTC into "P0301" form, per SAE J2012. */
static void decode_dtc(uint8_t a, uint8_t b, char out[6])
{
    static const char sys[4] = { 'P', 'C', 'B', 'U' };
    static const char hex[17] = "0123456789ABCDEF";

    out[0] = sys[(a >> 6) & 0x03];
    out[1] = hex[(a >> 4) & 0x03];
    out[2] = hex[a & 0x0F];
    out[3] = hex[(b >> 4) & 0x0F];
    out[4] = hex[b & 0x0F];
    out[5] = '\0';
}

static size_t parse_dtc_response(const char *resp, const char *marker,
                                 bool pending, size_t start_index)
{
    const char *p = strstr(resp, marker);
    if (p == NULL) {
        return start_index;
    }
    p += strlen(marker);

    uint8_t raw[SVC_OBD2_MAX_DTC * 2];
    const int n = hex_to_bytes(p, raw, sizeof(raw));
    if (n < 2) {
        return start_index;
    }

    size_t idx = start_index;
    for (int i = 0; (i + 1) < n && idx < SVC_OBD2_MAX_DTC; i += 2) {
        if (raw[i] == 0 && raw[i + 1] == 0) {
            continue;     /* padding, not a code */
        }
        decode_dtc(raw[i], raw[i + 1], s_dtcs[idx].code);
        s_dtcs[idx].pending = pending;
        idx++;
    }
    return idx;
}

esp_err_t svc_obd2_read_dtcs(void)
{
    if (s_status.state != SVC_OBD2_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    char resp[RX_BUF_SIZE];
    size_t count = 0;

    if (elm_cmd("03", resp, sizeof(resp), CMD_TIMEOUT_MS) == ESP_OK &&
        !reply_is_error(resp)) {
        count = parse_dtc_response(resp, "43", false, count);
    }
    if (elm_cmd("07", resp, sizeof(resp), CMD_TIMEOUT_MS) == ESP_OK &&
        !reply_is_error(resp)) {
        count = parse_dtc_response(resp, "47", true, count);
    }

    s_dtc_count = count;
    s_status.dtc_count = (uint8_t)count;

    /* Mode 01 PID 01 bit 7 of byte A is the malfunction indicator lamp. */
    uint8_t d[8] = { 0 };
    if (obd_query(0x01, 0x01, d, sizeof(d)) >= 1) {
        s_status.mil_on = (d[0] & 0x80) != 0;
    }

    ESP_LOGI(TAG, "%u trouble code(s), MIL %s",
             (unsigned)count, s_status.mil_on ? "on" : "off");
    svc_event_post(WATCH_EV_OBD2_DTC, NULL, 0);
    svc_event_post(WATCH_EV_OBD2_STATE, &s_status, sizeof(s_status));
    return ESP_OK;
}

size_t svc_obd2_get_dtcs(svc_obd2_dtc_t *out, size_t max)
{
    if (out == NULL) {
        return 0;
    }
    const size_t n = (s_dtc_count < max) ? s_dtc_count : max;
    memcpy(out, s_dtcs, n * sizeof(svc_obd2_dtc_t));
    return n;
}

esp_err_t svc_obd2_clear_dtcs(void)
{
    if (s_status.state != SVC_OBD2_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    char resp[64];
    ESP_RETURN_ON_ERROR(elm_cmd("04", resp, sizeof(resp), CMD_TIMEOUT_MS), TAG, "mode 04");
    if (reply_is_error(resp)) {
        set_error("clear rejected");
        return ESP_FAIL;
    }
    s_dtc_count = 0;
    s_status.dtc_count = 0;
    s_status.mil_on = false;
    svc_event_post(WATCH_EV_OBD2_DTC, NULL, 0);
    return ESP_OK;
}

esp_err_t svc_obd2_read_freeze_frame(uint8_t frame, svc_obd2_live_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_status.state != SVC_OBD2_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(out, 0, sizeof(*out));
    read_live_into(out, 0x02, frame);

    /* A frame with nothing valid in it means the ECU has never stored one. */
    if (!out->engine_load.valid && !out->coolant_temp.valid &&
        !out->rpm.valid && !out->speed.valid) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

/* --------------------------------------------------------------- monitors */

esp_err_t svc_obd2_read_monitors(svc_obd2_monitors_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_status.state != SVC_OBD2_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t d[8] = { 0 };
    const int n = obd_query(0x01, 0x01, d, sizeof(d));
    if (n < 4) {
        return ESP_ERR_NOT_FOUND;
    }

    /* Byte B: bits 0-2 say a test is available, bits 4-6 say it is still
     * incomplete. Available + complete = passed. */
    const uint8_t b = d[1];
    #define DECODE(avail_bit, incomplete_bit)                              \
        (((b & (1u << (avail_bit))) == 0) ? SVC_OBD2_MON_UNSUPPORTED       \
         : ((b & (1u << (incomplete_bit))) ? SVC_OBD2_MON_INCOMPLETE       \
                                           : SVC_OBD2_MON_OK))

    out->misfire     = DECODE(0, 4);
    out->fuel_system = DECODE(1, 5);
    out->components  = DECODE(2, 6);
    #undef DECODE

    /* Bytes C and D cover the emissions-related monitors. */
    const uint8_t c = d[2];
    const uint8_t dd = d[3];
    #define DECODE_CD(bit)                                                 \
        (((c & (1u << (bit))) == 0) ? SVC_OBD2_MON_UNSUPPORTED             \
         : ((dd & (1u << (bit))) ? SVC_OBD2_MON_INCOMPLETE                 \
                                 : SVC_OBD2_MON_OK))

    out->catalyst      = DECODE_CD(0);
    out->oxygen_sensor = DECODE_CD(5);
    out->egr_system    = DECODE_CD(7);
    #undef DECODE_CD

    /* The MIL being lit outranks a passed monitor. */
    if (d[0] & 0x80) {
        if (out->misfire == SVC_OBD2_MON_OK)     { out->misfire = SVC_OBD2_MON_FAULT; }
        if (out->fuel_system == SVC_OBD2_MON_OK) { out->fuel_system = SVC_OBD2_MON_FAULT; }
    }
    return ESP_OK;
}

/* ------------------------------------------------------------- connection */

static esp_err_t run_init_sequence(void)
{
    char resp[RX_BUF_SIZE];

    set_state(SVC_OBD2_CONNECTING);

    /* ATZ resets the adapter; it answers with its firmware banner. */
    if (elm_cmd("ATZ", resp, sizeof(resp), RESET_TIMEOUT_MS) != ESP_OK) {
        set_error("adapter did not answer ATZ");
        return ESP_ERR_TIMEOUT;
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Echo off, linefeeds off, spaces off, headers off. Everything after
     * this assumes compact replies. */
    (void)elm_cmd("ATE0", resp, sizeof(resp), CMD_TIMEOUT_MS);
    (void)elm_cmd("ATL0", resp, sizeof(resp), CMD_TIMEOUT_MS);
    (void)elm_cmd("ATS0", resp, sizeof(resp), CMD_TIMEOUT_MS);
    (void)elm_cmd("ATH0", resp, sizeof(resp), CMD_TIMEOUT_MS);
    (void)elm_cmd("ATSP0", resp, sizeof(resp), CMD_TIMEOUT_MS);   /* auto protocol */

    set_state(SVC_OBD2_LINK_READY);

    /* The first real request is what actually makes the adapter negotiate a
     * protocol with the vehicle, and it can take several seconds. The reply
     * usually arrives behind a "SEARCHING..." banner, so look for the
     * positive-response marker rather than judging the whole string. */
    if (elm_cmd("0100", resp, sizeof(resp), RESET_TIMEOUT_MS) != ESP_OK ||
        reply_is_error(resp) || strstr(resp, "4100") == NULL) {
        set_error("no response from the vehicle");
        ESP_LOGW(TAG, "adapter is up but the ECU is not answering");
        return ESP_ERR_NOT_FOUND;
    }

    if (elm_cmd("ATDP", resp, sizeof(resp), CMD_TIMEOUT_MS) == ESP_OK &&
        !reply_is_error(resp)) {
        strncpy(s_status.protocol, resp, sizeof(s_status.protocol) - 1);
        s_status.protocol[sizeof(s_status.protocol) - 1] = '\0';
    }

    set_state(SVC_OBD2_CONNECTED);
    set_error("");
    ESP_LOGI(TAG, "connected, protocol %s", s_status.protocol);

    (void)svc_obd2_read_dtcs();
    return ESP_OK;
}

esp_err_t svc_obd2_connect_addr(const uint8_t addr[6], uint8_t addr_type)
{
    ESP_RETURN_ON_ERROR(svc_ble_set_enabled(true), TAG, "ble on");
    svc_ble_set_notify_cb(on_ble_notify, NULL);

    set_state(SVC_OBD2_CONNECTING);
    esp_err_t err = svc_ble_connect(addr, addr_type);
    if (err != ESP_OK) {
        set_error("could not open the adapter link");
        set_state(SVC_OBD2_ERROR);
        return err;
    }

    const svc_ble_status_t *ble = svc_ble_status();
    strncpy(s_status.adapter_name, ble->peer.name, sizeof(s_status.adapter_name) - 1);
    s_status.adapter_name[sizeof(s_status.adapter_name) - 1] = '\0';

    err = run_init_sequence();
    if (err != ESP_OK && s_status.state != SVC_OBD2_LINK_READY) {
        set_state(SVC_OBD2_ERROR);
        return err;
    }

    /* Remember a working adapter so the next connect skips the scan. */
    (void)svc_settings_save_obd2_addr(addr, addr_type);
    return err;
}

esp_err_t svc_obd2_connect(void)
{
    uint8_t addr[6];
    uint8_t addr_type = 0;

    if (svc_settings_load_obd2_addr(addr, &addr_type) == ESP_OK) {
        ESP_LOGI(TAG, "trying the saved adapter");
        if (svc_obd2_connect_addr(addr, addr_type) == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "saved adapter did not answer, scanning");
    }

    set_state(SVC_OBD2_SEARCHING);
    ESP_RETURN_ON_ERROR(svc_ble_set_enabled(true), TAG, "ble on");
    ESP_RETURN_ON_ERROR(svc_ble_scan_start(SCAN_DURATION_MS), TAG, "scan");

    /* svc_ble posts WATCH_EV_BLE_SCAN_DONE, but this call is synchronous by
     * contract, so wait the scan out here. */
    vTaskDelay(pdMS_TO_TICKS(SCAN_DURATION_MS + 500));

    svc_ble_device_t devs[SVC_BLE_MAX_SCAN];
    const size_t n = svc_ble_get_scan(devs, SVC_BLE_MAX_SCAN);

    /* The list is sorted by signal strength, so the first match is the
     * closest adapter - which is almost always the one in this car. */
    for (size_t i = 0; i < n; i++) {
        if (!devs[i].looks_like_obd2) {
            continue;
        }
        ESP_LOGI(TAG, "trying %s (%d dBm)", devs[i].name, devs[i].rssi);
        if (svc_obd2_connect_addr(devs[i].addr, devs[i].addr_type) == ESP_OK) {
            return ESP_OK;
        }
    }

    set_error("no adapter found");
    set_state(SVC_OBD2_ERROR);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t svc_obd2_disconnect(void)
{
    s_polling = false;
    svc_ble_set_notify_cb(NULL, NULL);
    const esp_err_t err = svc_ble_disconnect();
    memset(&s_live, 0, sizeof(s_live));
    s_dtc_count = 0;
    s_status.dtc_count = 0;
    s_status.protocol[0] = '\0';
    set_state(SVC_OBD2_OFF);
    return err;
}

esp_err_t svc_obd2_set_polling(bool enable)
{
    if (enable && s_status.state != SVC_OBD2_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_polling = enable;
    return ESP_OK;
}

const svc_obd2_status_t *svc_obd2_status(void)
{
    return &s_status;
}

const svc_obd2_live_t *svc_obd2_live(void)
{
    return &s_live;
}

/* ------------------------------------------------------- DTC descriptions */

/* The full J2012 list runs to thousands of codes and most of them are
 * manufacturer-specific. This covers the generic powertrain codes a driver
 * is most likely to meet; anything else is shown as its bare code. */
typedef struct {
    const char *code;
    const char *text;
} dtc_desc_t;

static const dtc_desc_t k_dtc_desc[] = {
    { "P0100", "Mass air flow circuit" },
    { "P0101", "Mass air flow range/performance" },
    { "P0110", "Intake air temperature circuit" },
    { "P0115", "Engine coolant temperature circuit" },
    { "P0120", "Throttle position sensor circuit" },
    { "P0128", "Coolant thermostat below regulating temperature" },
    { "P0130", "O2 sensor circuit (bank 1 sensor 1)" },
    { "P0133", "O2 sensor slow response (bank 1 sensor 1)" },
    { "P0135", "O2 sensor heater circuit (bank 1 sensor 1)" },
    { "P0171", "System too lean (bank 1)" },
    { "P0172", "System too rich (bank 1)" },
    { "P0174", "System too lean (bank 2)" },
    { "P0175", "System too rich (bank 2)" },
    { "P0300", "Random/multiple cylinder misfire" },
    { "P0301", "Cylinder 1 misfire detected" },
    { "P0302", "Cylinder 2 misfire detected" },
    { "P0303", "Cylinder 3 misfire detected" },
    { "P0304", "Cylinder 4 misfire detected" },
    { "P0305", "Cylinder 5 misfire detected" },
    { "P0306", "Cylinder 6 misfire detected" },
    { "P0325", "Knock sensor circuit" },
    { "P0335", "Crankshaft position sensor circuit" },
    { "P0340", "Camshaft position sensor circuit" },
    { "P0401", "EGR flow insufficient" },
    { "P0403", "EGR control circuit" },
    { "P0420", "Catalyst system efficiency below threshold (bank 1)" },
    { "P0430", "Catalyst system efficiency below threshold (bank 2)" },
    { "P0440", "Evaporative emission system" },
    { "P0442", "Evaporative emission system small leak" },
    { "P0446", "Evaporative emission vent control circuit" },
    { "P0455", "Evaporative emission system gross leak" },
    { "P0500", "Vehicle speed sensor" },
    { "P0505", "Idle air control system" },
    { "P0506", "Idle speed lower than expected" },
    { "P0507", "Idle speed higher than expected" },
    { "P0700", "Transmission control system" },
    { "P0730", "Incorrect gear ratio" },
    { "P0740", "Torque converter clutch circuit" },
    { "U0100", "Lost communication with ECM/PCM" },
    { "U0121", "Lost communication with ABS control module" },
    { "U0155", "Lost communication with instrument cluster" },
    { "C0035", "Left front wheel speed sensor" },
    { "C0040", "Right front wheel speed sensor" },
    { "B0001", "Driver airbag deployment control" },
};

const char *svc_obd2_describe_dtc(const char *code)
{
    if (code == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(k_dtc_desc) / sizeof(k_dtc_desc[0]); i++) {
        if (strcasecmp(code, k_dtc_desc[i].code) == 0) {
            return k_dtc_desc[i].text;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ init */

esp_err_t svc_obd2_init(void)
{
    s_prompt_sem = xSemaphoreCreateBinary();
    s_bus_mutex  = xSemaphoreCreateMutex();
    if (s_prompt_sem == NULL || s_bus_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_status.state = SVC_OBD2_OFF;

    if (xTaskCreatePinnedToCore(poll_task, "obd2_poll", 5120, NULL, 3, &s_poll_task, 0)
        != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
