#include "watch_hal/pcf85063.h"
#include "watch_hal/watch_board.h"

#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pcf85063";

#define REG_CONTROL1   0x00
#define REG_CONTROL2   0x01
#define REG_OFFSET     0x02
#define REG_SECONDS    0x04   /* bit7 = OS, oscillator-stopped flag */
#define REG_MINUTES    0x05
#define REG_HOURS      0x06
#define REG_DAYS       0x07
#define REG_WEEKDAYS   0x08
#define REG_MONTHS     0x09
#define REG_YEARS      0x0A

#define CTRL1_STOP     (1u << 5)
#define CTRL1_SR       (1u << 4)   /* software reset, write 0x58 pattern */
#define SOFT_RESET_CMD 0x58

#define I2C_TIMEOUT_MS 100

static i2c_master_dev_handle_t s_dev;
static bool                    s_present;

/* ------------------------------------------------------------------ helpers */

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t len)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, I2C_TIMEOUT_MS);
}

static esp_err_t wr(uint8_t reg, const uint8_t *data, size_t len)
{
    if (!s_dev || len > 8) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[9];
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(s_dev, buf, len + 1, I2C_TIMEOUT_MS);
}

static esp_err_t wr8(uint8_t reg, uint8_t val)
{
    return wr(reg, &val, 1);
}

static uint8_t bcd2dec(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + ((v >> 4) * 10));
}

static uint8_t dec2bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

/* ---------------------------------------------------------------------- api */

esp_err_t pcf85063_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dev) {
        return ESP_OK;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = WATCH_I2C_ADDR_RTC_PCF85063,
        .scl_speed_hz    = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG,
                        "cannot add RTC to I2C bus");

    uint8_t ctrl1 = 0;
    if (rd(REG_CONTROL1, &ctrl1, 1) != ESP_OK) {
        ESP_LOGE(TAG, "no PCF85063 at 0x%02X", WATCH_I2C_ADDR_RTC_PCF85063);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    /* Clear STOP so the counter runs. Deliberately not a software reset -
     * that would wipe a time the RTC has been keeping across a power cut. */
    if (ctrl1 & CTRL1_STOP) {
        ESP_RETURN_ON_ERROR(wr8(REG_CONTROL1, (uint8_t)(ctrl1 & ~CTRL1_STOP)), TAG, "start osc");
    }

    s_present = true;
    ESP_LOGI(TAG, "PCF85063 ready");
    return ESP_OK;
}

bool pcf85063_is_present(void)
{
    return s_present;
}

esp_err_t pcf85063_get_time(struct tm *out, bool *out_valid)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t r[7];
    ESP_RETURN_ON_ERROR(rd(REG_SECONDS, r, sizeof(r)), TAG, "read time");

    const bool osc_stopped = (r[0] & 0x80) != 0;
    if (out_valid != NULL) {
        *out_valid = !osc_stopped;
    }

    memset(out, 0, sizeof(*out));
    out->tm_sec  = bcd2dec(r[0] & 0x7F);
    out->tm_min  = bcd2dec(r[1] & 0x7F);
    out->tm_hour = bcd2dec(r[2] & 0x3F);   /* configured for 24-hour mode */
    out->tm_mday = bcd2dec(r[3] & 0x3F);
    out->tm_wday = r[4] & 0x07;
    out->tm_mon  = (int)bcd2dec(r[5] & 0x1F) - 1;
    out->tm_year = (int)bcd2dec(r[6]) + 100; /* register holds 00-99 => 2000-2099 */
    out->tm_isdst = 0;
    return ESP_OK;
}

esp_err_t pcf85063_set_time(const struct tm *t)
{
    if (t == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    if (t->tm_year < 100 || t->tm_year > 199) {
        return ESP_ERR_INVALID_ARG;   /* the chip only spans 2000-2099 */
    }

    /* Writing seconds with bit7 clear also clears the oscillator-stop flag. */
    const uint8_t r[7] = {
        dec2bcd((uint8_t)t->tm_sec) & 0x7F,
        dec2bcd((uint8_t)t->tm_min),
        dec2bcd((uint8_t)t->tm_hour),
        dec2bcd((uint8_t)t->tm_mday),
        (uint8_t)(t->tm_wday & 0x07),
        dec2bcd((uint8_t)(t->tm_mon + 1)),
        dec2bcd((uint8_t)(t->tm_year - 100)),
    };
    return wr(REG_SECONDS, r, sizeof(r));
}

/*
 * Broken-down UTC to a Unix timestamp.
 *
 * Written out rather than calling timegm(), which is a glibc extension that
 * newlib does not reliably provide, and mktime(), which would apply the
 * local timezone - wrong here, because the RTC holds UTC and the timezone
 * is already set by the time this runs.
 *
 * Howard Hinnant's days_from_civil: exact for every date the chip can hold.
 */
static int64_t utc_to_epoch(const struct tm *t)
{
    int32_t y = t->tm_year + 1900;
    const uint32_t m = (uint32_t)(t->tm_mon + 1);
    const uint32_t d = (uint32_t)t->tm_mday;

    y -= (m <= 2);
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = (uint32_t)(y - era * 400);                 /* 0..399 */
    const uint32_t doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;  /* 0..146096 */
    const int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;

    return days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
}

esp_err_t pcf85063_sync_to_system(void)
{
    struct tm tm_rtc;
    bool valid = false;
    ESP_RETURN_ON_ERROR(pcf85063_get_time(&tm_rtc, &valid), TAG, "rtc read");

    if (!valid) {
        ESP_LOGW(TAG, "RTC lost power - not trusting its time");
        return ESP_ERR_INVALID_STATE;
    }

    const time_t utc = (time_t)utc_to_epoch(&tm_rtc);
    if (utc <= 0) {
        return ESP_ERR_INVALID_STATE;
    }
    const struct timeval tv = { .tv_sec = utc, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "system clock set from RTC (%04d-%02d-%02d %02d:%02d:%02d UTC)",
             tm_rtc.tm_year + 1900, tm_rtc.tm_mon + 1, tm_rtc.tm_mday,
             tm_rtc.tm_hour, tm_rtc.tm_min, tm_rtc.tm_sec);
    return ESP_OK;
}

esp_err_t pcf85063_sync_from_system(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }
    struct tm tm_utc;
    if (gmtime_r(&tv.tv_sec, &tm_utc) == NULL) {
        return ESP_FAIL;
    }
    return pcf85063_set_time(&tm_utc);
}
