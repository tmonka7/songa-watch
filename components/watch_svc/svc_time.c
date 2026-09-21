#include "watch_svc/svc_time.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_settings.h"
#include "watch_svc/svc_i18n.h"
#include "watch_hal/pcf85063.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_timer.h"

#include "bsp/esp-bsp.h"

static const char *TAG = "svc_time";

static bool s_synced;
static bool s_sntp_started;

/* A short list is friendlier than a full tz database on a 410x502 screen,
 * and it keeps the POSIX strings under the app's control. */
typedef struct {
    const char *label;
    const char *posix;
} tz_entry_t;

static const tz_entry_t s_tz[] = {
    { "UTC",              "UTC0"                    },
    { "London (UK)",      "GMT0BST,M3.5.0/1,M10.5.0"},
    { "Berlin (CET)",     "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Dubai (UTC+4)",    "GST-4"                   },
    { "Karachi (UTC+5)",  "PKT-5"                   },
    { "Dhaka (UTC+6)",    "BDT-6"                   },
    { "Bangkok (UTC+7)",  "ICT-7"                   },
    { "Singapore (UTC+8)","SGT-8"                   },
    { "Seoul (UTC+9)",    "KST-9"                   },
    { "Tokyo (UTC+9)",    "JST-9"                   },
    { "Sydney (AEST)",    "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "New York (EST)",   "EST5EDT,M3.2.0,M11.1.0"  },
    { "Chicago (CST)",    "CST6CDT,M3.2.0,M11.1.0"  },
    { "Denver (MST)",     "MST7MDT,M3.2.0,M11.1.0"  },
    { "Los Angeles (PST)","PST8PDT,M3.2.0,M11.1.0"  },
};

static const char *const s_wday_en[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const s_wday_jp[7] = { "日", "月", "火", "水", "木", "金", "土" };
static const char *const s_mon_en[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static void apply_tz(const char *posix_tz)
{
    setenv("TZ", (posix_tz != NULL && posix_tz[0] != '\0') ? posix_tz : "UTC0", 1);
    tzset();
}

static void sntp_sync_cb(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    ESP_LOGI(TAG, "SNTP sync complete");

    /* Push the corrected time down to the RTC so it survives a power cut. */
    if (pcf85063_is_present()) {
        if (pcf85063_sync_from_system() == ESP_OK) {
            ESP_LOGI(TAG, "RTC updated from SNTP");
        }
    }
    svc_event_post(WATCH_EV_TIME_SYNCED, NULL, 0);
}

esp_err_t svc_time_init(void)
{
    apply_tz(svc_settings_get()->tz);

    if (pcf85063_init(bsp_i2c_get_handle()) == ESP_OK) {
        if (pcf85063_sync_to_system() != ESP_OK) {
            ESP_LOGW(TAG, "RTC had no usable time; clock starts at the epoch");
        }
    } else {
        ESP_LOGW(TAG, "no RTC: the clock will not survive a power cut");
    }
    return ESP_OK;
}

void svc_time_now(struct tm *out)
{
    if (out == NULL) {
        return;
    }
    const time_t now = time(NULL);
    localtime_r(&now, out);
}

esp_err_t svc_time_set_local(const struct tm *local)
{
    if (local == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* mktime() reads the tm as local time under the current TZ, which is
     * exactly what the picker hands us. tm_isdst = -1 lets it work out
     * whether summer time applies on that date. */
    struct tm copy = *local;
    copy.tm_isdst = -1;
    const time_t utc = mktime(&copy);
    if (utc == (time_t)-1) {
        return ESP_ERR_INVALID_ARG;
    }

    const struct timeval tv = { .tv_sec = utc, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }

    if (pcf85063_is_present()) {
        (void)pcf85063_sync_from_system();
    }
    s_synced = true;
    svc_event_post(WATCH_EV_TIME_SYNCED, NULL, 0);
    return ESP_OK;
}

esp_err_t svc_time_set_timezone(const char *posix_tz)
{
    ESP_RETURN_ON_ERROR(svc_settings_set_timezone(posix_tz), TAG, "save tz");
    apply_tz(posix_tz);
    svc_event_post(WATCH_EV_TIME_SYNCED, NULL, 0);
    return ESP_OK;
}

esp_err_t svc_time_sync_now(void)
{
    if (s_sntp_started) {
        esp_netif_sntp_deinit();
        s_sntp_started = false;
    }

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.start = true;
    cfg.sync_cb = sntp_sync_cb;
    cfg.renew_servers_after_new_IP = true;

    ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&cfg), TAG, "sntp init");
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started");
    return ESP_OK;
}

bool svc_time_is_synced(void)
{
    return s_synced;
}

void svc_time_format_clock(char *buf, size_t len)
{
    if (buf == NULL || len < 6) {
        return;
    }
    struct tm t;
    svc_time_now(&t);

    if (svc_settings_get()->time_24h) {
        snprintf(buf, len, "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        int h = t.tm_hour % 12;
        if (h == 0) { h = 12; }
        snprintf(buf, len, "%d:%02d", h, t.tm_min);
    }
}

void svc_time_format_ampm(char *buf, size_t len)
{
    if (buf == NULL || len < 3) {
        return;
    }
    if (svc_settings_get()->time_24h) {
        buf[0] = '\0';
        return;
    }
    struct tm t;
    svc_time_now(&t);
    snprintf(buf, len, "%s", (t.tm_hour < 12) ? "AM" : "PM");
}

void svc_time_format_date(char *buf, size_t len)
{
    if (buf == NULL || len < 8) {
        return;
    }
    struct tm t;
    svc_time_now(&t);

    const int wd = (t.tm_wday >= 0 && t.tm_wday < 7) ? t.tm_wday : 0;
    const int mo = (t.tm_mon >= 0 && t.tm_mon < 12) ? t.tm_mon : 0;

    if (i18n_get_lang() == WATCH_LANG_JP) {
        /* Japanese convention: 9月20日(土) */
        snprintf(buf, len, "%d%s%d%s(%s)",
                 mo + 1, i18n(STR_MONTH), t.tm_mday, i18n(STR_DAY), s_wday_jp[wd]);
    } else {
        snprintf(buf, len, "%s, %s %d", s_wday_en[wd], s_mon_en[mo], t.tm_mday);
    }
}

void svc_time_format_uptime(char *buf, size_t len)
{
    if (buf == NULL || len < 8) {
        return;
    }
    const int64_t us = esp_timer_get_time();
    const uint32_t total_sec = (uint32_t)(us / 1000000);
    const uint32_t days = total_sec / 86400;
    const uint32_t hours = (total_sec % 86400) / 3600;
    const uint32_t mins = (total_sec % 3600) / 60;

    if (days > 0) {
        snprintf(buf, len, "%lud %02lu:%02lu",
                 (unsigned long)days, (unsigned long)hours, (unsigned long)mins);
    } else {
        snprintf(buf, len, "%02lu:%02lu:%02lu",
                 (unsigned long)hours, (unsigned long)mins,
                 (unsigned long)(total_sec % 60));
    }
}

size_t svc_time_tz_count(void)
{
    return sizeof(s_tz) / sizeof(s_tz[0]);
}

const char *svc_time_tz_label(size_t index)
{
    return (index < svc_time_tz_count()) ? s_tz[index].label : "";
}

const char *svc_time_tz_posix(size_t index)
{
    return (index < svc_time_tz_count()) ? s_tz[index].posix : "UTC0";
}
