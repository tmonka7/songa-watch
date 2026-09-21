#include "watch_hal/axp2101.h"
#include "watch_hal/watch_board.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "axp2101";

/* ---------------------------------------------------------------- registers */
#define REG_STATUS1          0x00
#define REG_STATUS2          0x01
#define REG_COMMON_CONFIG    0x10
#define REG_INTEN1           0x40
#define REG_INTEN2           0x41
#define REG_INTEN3           0x42
#define REG_INTSTS1          0x48
#define REG_INTSTS2          0x49
#define REG_INTSTS3          0x4A
#define REG_ADC_CH_CTRL      0x30
#define REG_ADC_RESULT0      0x34   /* vbat  H5L8 */
#define REG_ADC_RESULT4      0x38   /* vbus  H6L8 */
#define REG_ADC_RESULT6      0x3A   /* vsys  H6L8 */
#define REG_ADC_RESULT8      0x3C   /* die temp H6L8 */
#define REG_IPRECHG_SET      0x61
#define REG_ICC_CHG_SET      0x62
#define REG_ITERM_CHG_SET    0x63
#define REG_CV_CHG_VOL_SET   0x64
#define REG_BAT_DET_CTRL     0x68
#define REG_BAT_PERCENT      0xA4

#define CHG_VOL_4V2          3       /* REG 0x64 [2:0] */
#define PRECHG_50MA          1       /* REG 0x61 [1:0], 25 mA per step */
#define ITERM_25MA           1       /* REG 0x63 [3:0], 25 mA per step */

#define I2C_TIMEOUT_MS       100

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

static esp_err_t rd8(uint8_t reg, uint8_t *val)
{
    return rd(reg, val, 1);
}

static esp_err_t wr8(uint8_t reg, uint8_t val)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t update_bits(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t cur;
    esp_err_t err = rd8(reg, &cur);
    if (err != ESP_OK) {
        return err;
    }
    const uint8_t next = (uint8_t)((cur & ~mask) | (value & mask));
    return (next == cur) ? ESP_OK : wr8(reg, next);
}

/* The ADC results are big-endian pairs whose high byte carries only the top
 * few bits; the rest of that byte is status and must be masked off. */
static esp_err_t rd_adc(uint8_t reg, uint8_t high_mask, uint16_t *out)
{
    uint8_t b[2];
    esp_err_t err = rd(reg, b, 2);
    if (err != ESP_OK) {
        return err;
    }
    *out = (uint16_t)(((b[0] & high_mask) << 8) | b[1]);
    return ESP_OK;
}

/* ---------------------------------------------------------------------- api */

esp_err_t axp2101_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dev) {
        return ESP_OK;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = WATCH_I2C_ADDR_PMU_AXP2101,
        .scl_speed_hz    = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG,
                        "cannot add PMU to I2C bus");

    uint8_t probe;
    if (rd8(REG_STATUS1, &probe) != ESP_OK) {
        ESP_LOGE(TAG, "no AXP2101 at 0x%02X", WATCH_I2C_ADDR_PMU_AXP2101);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    /* Measure battery, VBUS, VSYS and die temperature. Leave the TS (battery
     * thermistor) channel off: this board has no thermistor fitted, and an
     * open TS input makes the charger refuse to charge. */
    ESP_RETURN_ON_ERROR(wr8(REG_ADC_CH_CTRL, 0x1D), TAG, "adc enable");

    /* Charger: 50 mA pre-charge, 400 mA CC, 25 mA termination, 4.2 V float. */
    ESP_RETURN_ON_ERROR(update_bits(REG_IPRECHG_SET, 0x03, PRECHG_50MA), TAG, "iprechg");
    ESP_RETURN_ON_ERROR(update_bits(REG_ICC_CHG_SET, 0x1F, AXP2101_ICC_400MA), TAG, "icc");
    ESP_RETURN_ON_ERROR(update_bits(REG_ITERM_CHG_SET, 0x0F, ITERM_25MA), TAG, "iterm");
    ESP_RETURN_ON_ERROR(update_bits(REG_CV_CHG_VOL_SET, 0x07, CHG_VOL_4V2), TAG, "cv");

    /* Battery presence detection feeds the fuel gauge. */
    ESP_RETURN_ON_ERROR(update_bits(REG_BAT_DET_CTRL, 0x01, 0x01), TAG, "bat detect");

    /* Mask everything, clear the latches, then enable only what we act on.
     * INTEN1 bit1 = BAT_INSERT, bit0 = BAT_REMOVE (0x03)
     *        bit7 = VBUS_INSERT? -> see the enable map below.
     * The AXP2101 IRQ map, by register:
     *   INTEN1: b7 BAT_OVER_VOLT, b6 CHG_TIMEOUT, b5 DIE_OVER_TEMP,
     *           b4 BAT_CHG_DONE,  b3 BAT_CHG_START,
     *           b2 BAT_REMOVE,    b1 BAT_INSERT,  b0 GAUGE_NEW_SOC
     *   INTEN2: b7 PKEY_LONG, b6 PKEY_SHORT, b5 PKEY_NEGATIVE,
     *           b4 PKEY_POSITIVE, b3 VBUS_REMOVE, b2 VBUS_INSERT, ...
     *   INTEN3: b1 BAT_LOW_WARN (SOC drop to warning level 1) */
    (void)wr8(REG_INTEN1, 0x00);
    (void)wr8(REG_INTEN2, 0x00);
    (void)wr8(REG_INTEN3, 0x00);
    (void)wr8(REG_INTSTS1, 0xFF);
    (void)wr8(REG_INTSTS2, 0xFF);
    (void)wr8(REG_INTSTS3, 0xFF);

    (void)wr8(REG_INTEN1, 0x1E);  /* chg done, chg start, bat remove, bat insert */
    (void)wr8(REG_INTEN2, 0xCC);  /* pkey long, pkey short, vbus remove, vbus insert */
    (void)wr8(REG_INTEN3, 0x02);  /* battery low warning */

    s_present = true;
    ESP_LOGI(TAG, "AXP2101 ready (charger 400 mA CC / 4.2 V, TS pin disabled)");
    return ESP_OK;
}

bool axp2101_is_present(void)
{
    return s_present;
}

esp_err_t axp2101_read_status(axp2101_status_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(out, 0, sizeof(*out));

    uint8_t st1 = 0, st2 = 0;
    ESP_RETURN_ON_ERROR(rd8(REG_STATUS1, &st1), TAG, "status1");
    ESP_RETURN_ON_ERROR(rd8(REG_STATUS2, &st2), TAG, "status2");

    out->vbus_present    = (st1 & (1u << 5)) != 0;
    out->battery_present = (st1 & (1u << 3)) != 0;

    /* STATUS2 [6:5]: 0 = standby, 1 = charging, 2 = discharging. */
    const uint8_t dir = (uint8_t)((st2 >> 5) & 0x03);
    out->charging  = (dir == 1);
    out->chg_state = (axp2101_chg_state_t)(st2 & 0x07);
    if (out->chg_state > AXP2101_CHG_STOP) {
        out->chg_state = AXP2101_CHG_STOP;
    }

    uint16_t raw;
    if (out->battery_present && rd_adc(REG_ADC_RESULT0, 0x1F, &raw) == ESP_OK) {
        out->vbat_mv = raw;
    }
    if (out->vbus_present && rd_adc(REG_ADC_RESULT4, 0x3F, &raw) == ESP_OK) {
        out->vbus_mv = raw;
    }
    if (rd_adc(REG_ADC_RESULT6, 0x3F, &raw) == ESP_OK) {
        out->vsys_mv = raw;
    }
    if (rd_adc(REG_ADC_RESULT8, 0x3F, &raw) == ESP_OK) {
        /* Datasheet transfer function for the internal die sensor. */
        out->die_temp_c = 22.0f + (7274.0f - (float)raw) / 20.0f;
    }

    if (out->battery_present) {
        uint8_t pct = 0;
        if (rd8(REG_BAT_PERCENT, &pct) == ESP_OK && pct <= 100) {
            out->percent = pct;
        } else {
            out->percent = 255;
        }
    } else {
        out->percent = 255;
    }

    /* The AXP2101 has no battery-current ADC. Report the sign of the flow so
     * callers can render a direction; magnitude is not available. */
    out->ibat_ma = out->charging ? 1 : (out->battery_present ? -1 : 0);

    return ESP_OK;
}

esp_err_t axp2101_irq_read(uint32_t *out_flags)
{
    if (out_flags == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_flags = 0;
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t sts[3] = { 0 };
    ESP_RETURN_ON_ERROR(rd(REG_INTSTS1, sts, sizeof(sts)), TAG, "intsts");

    uint32_t f = 0;
    if (sts[0] & (1u << 1)) f |= AXP2101_IRQ_BAT_INSERT;
    if (sts[0] & (1u << 2)) f |= AXP2101_IRQ_BAT_REMOVE;
    if (sts[0] & (1u << 3)) f |= AXP2101_IRQ_CHG_START;
    if (sts[0] & (1u << 4)) f |= AXP2101_IRQ_CHG_DONE;
    if (sts[1] & (1u << 2)) f |= AXP2101_IRQ_VBUS_INSERT;
    if (sts[1] & (1u << 3)) f |= AXP2101_IRQ_VBUS_REMOVE;
    if (sts[1] & (1u << 6)) f |= AXP2101_IRQ_PWRON_SHORT;
    if (sts[1] & (1u << 7)) f |= AXP2101_IRQ_PWRON_LONG;
    if (sts[2] & (1u << 1)) f |= AXP2101_IRQ_BAT_LOW;
    *out_flags = f;

    /* Write-1-to-clear; only clear what we actually latched. */
    for (int i = 0; i < 3; i++) {
        if (sts[i]) {
            (void)wr8((uint8_t)(REG_INTSTS1 + i), sts[i]);
        }
    }
    return ESP_OK;
}

esp_err_t axp2101_set_charge_current(axp2101_icc_t icc)
{
    return update_bits(REG_ICC_CHG_SET, 0x1F, (uint8_t)icc);
}

esp_err_t axp2101_power_off(void)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGW(TAG, "powering off - all rails except VRTC");
    /* COMMON_CONFIG bit0 = SOFT_PWROFF. */
    esp_err_t err = update_bits(REG_COMMON_CONFIG, 0x01, 0x01);
    vTaskDelay(pdMS_TO_TICKS(200));
    return err;
}
