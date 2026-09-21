/*
 * Timekeeping.
 *
 * The PCF85063 is the source of truth across reboots and power cuts; SNTP
 * corrects it when Wi-Fi is up. Everything the UI formats goes through here
 * so the 12/24-hour setting and the timezone are applied in exactly one
 * place.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Read the RTC into the system clock and apply the saved timezone. */
esp_err_t svc_time_init(void);

/** @brief Local broken-down time. */
void svc_time_now(struct tm *out);

/**
 * @brief Set the clock from the user's input, in local time.
 *
 * Writes through to the RTC so the change survives a power cut.
 */
esp_err_t svc_time_set_local(const struct tm *local);

/** @brief Apply a POSIX TZ string (e.g. "JST-9") and persist it. */
esp_err_t svc_time_set_timezone(const char *posix_tz);

/** @brief Kick off an SNTP sync. Needs Wi-Fi; returns immediately. */
esp_err_t svc_time_sync_now(void);

/** @brief True once the clock has been set from SNTP this boot. */
bool svc_time_is_synced(void);

/** @brief "19:42" or "7:42 PM", per the 24-hour setting. @p len >= 12. */
void svc_time_format_clock(char *buf, size_t len);

/** @brief "Sat, Sep 20", localised. @p len >= 24. */
void svc_time_format_date(char *buf, size_t len);

/** @brief "AM"/"PM", or an empty string in 24-hour mode. @p len >= 4. */
void svc_time_format_ampm(char *buf, size_t len);

/** @brief Seconds since boot, formatted as "3d 04:21". @p len >= 20. */
void svc_time_format_uptime(char *buf, size_t len);

/** @brief A short list of common POSIX timezones for the settings picker. */
size_t svc_time_tz_count(void);
const char *svc_time_tz_label(size_t index);   /* "Tokyo (UTC+9)" */
const char *svc_time_tz_posix(size_t index);   /* "JST-9" */

#ifdef __cplusplus
}
#endif
