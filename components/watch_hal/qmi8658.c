#include "watch_hal/qmi8658.h"
#include "watch_hal/watch_board.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "qmi8658";

/* ---------------------------------------------------------------- registers */
#define REG_WHOAMI        0x00
#define REG_REVISION      0x01
#define REG_CTRL1         0x02
#define REG_CTRL2         0x03   /* accel: [6:4] range, [3:0] ODR */
#define REG_CTRL3         0x04   /* gyro:  [6:4] range, [3:0] ODR */
#define REG_CTRL5         0x06   /* low-pass filters */
#define REG_CTRL7         0x08   /* enable bits */
#define REG_CTRL8         0x09   /* motion engine control */
#define REG_CTRL9         0x0A   /* command register */
#define REG_STATUS0       0x2E
#define REG_TEMP_L        0x33
#define REG_AX_L          0x35
#define REG_STEP_CNT_LOW  0x5A
#define REG_STEP_CNT_MID  0x5B
#define REG_STEP_CNT_HIGH 0x5C
#define REG_RESET         0x60

#define WHOAMI_VALUE      0x05
#define RESET_CMD         0xB0

#define CTRL7_ACC_EN      (1u << 0)
#define CTRL7_GYR_EN      (1u << 1)

#define CTRL8_PED_EN      (1u << 4)

#define CTRL9_CMD_CONFIGURE_PEDOMETER 0x0D
#define CTRL9_CMD_RESET_PEDOMETER     0x0F

#define I2C_TIMEOUT_MS    100

static i2c_master_dev_handle_t s_dev;
static bool                    s_present;
static qmi8658_config_t        s_cfg;
static float                   s_acc_scale;   /* LSB -> g */
static float                   s_gyr_scale;   /* LSB -> dps */

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
    ESP_RETURN_ON_ERROR(rd8(reg, &cur), TAG, "rd 0x%02X", reg);
    const uint8_t next = (uint8_t)((cur & ~mask) | (value & mask));
    return (next == cur) ? ESP_OK : wr8(reg, next);
}

static void recalc_scales(void)
{
    /* Both sensors are 16-bit signed full scale. */
    static const float acc_g[]   = { 2.0f, 4.0f, 8.0f, 16.0f };
    static const float gyr_dps[] = { 16.0f, 32.0f, 64.0f, 128.0f,
                                     256.0f, 512.0f, 1024.0f, 2048.0f };
    s_acc_scale = acc_g[s_cfg.acc_range & 0x03] / 32768.0f;
    s_gyr_scale = gyr_dps[s_cfg.gyr_range & 0x07] / 32768.0f;
}

static esp_err_t apply_config(void)
{
    ESP_RETURN_ON_ERROR(wr8(REG_CTRL2,
                            (uint8_t)((s_cfg.acc_range << 4) | (s_cfg.acc_odr & 0x0F))),
                        TAG, "ctrl2");
    ESP_RETURN_ON_ERROR(wr8(REG_CTRL3,
                            (uint8_t)((s_cfg.gyr_range << 4) | (s_cfg.gyr_odr & 0x0F))),
                        TAG, "ctrl3");
    /* Low-pass both paths at the widest setting (mode 0, ~2.66% of ODR) to
     * knock out the high-frequency noise the graph screen would otherwise
     * show as fuzz. CTRL5: b0 aLPF_EN, [2:1] aLPF_MODE, b4 gLPF_EN, [6:5] gLPF_MODE. */
    ESP_RETURN_ON_ERROR(wr8(REG_CTRL5, 0x11), TAG, "ctrl5");
    recalc_scales();
    return ESP_OK;
}

/* ---------------------------------------------------------------------- api */

esp_err_t qmi8658_init(i2c_master_bus_handle_t bus, const qmi8658_config_t *cfg)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_dev) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = WATCH_I2C_ADDR_IMU_QMI8658,
            .scl_speed_hz    = 400000,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG,
                            "cannot add IMU to I2C bus");

        uint8_t who = 0;
        if (rd8(REG_WHOAMI, &who) != ESP_OK || who != WHOAMI_VALUE) {
            /* Try the alternate address before giving up - SA0 strapping
             * differs between board revisions. */
            i2c_master_bus_rm_device(s_dev);
            s_dev = NULL;
            dev_cfg.device_address = 0x6A;
            ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG,
                                "cannot add IMU to I2C bus");
            if (rd8(REG_WHOAMI, &who) != ESP_OK || who != WHOAMI_VALUE) {
                ESP_LOGE(TAG, "no QMI8658 at 0x6B or 0x6A (WHO_AM_I=0x%02X)", who);
                i2c_master_bus_rm_device(s_dev);
                s_dev = NULL;
                return ESP_ERR_NOT_FOUND;
            }
        }
    }

    /* Soft reset, then wait for the chip to come back. */
    ESP_RETURN_ON_ERROR(wr8(REG_RESET, RESET_CMD), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(20));

    /* CTRL1: address auto-increment on for burst reads, little-endian data,
     * internal oscillator running. */
    ESP_RETURN_ON_ERROR(wr8(REG_CTRL1, 0x40), TAG, "ctrl1");

    s_cfg = (cfg != NULL) ? *cfg : QMI8658_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(apply_config(), TAG, "config");
    ESP_RETURN_ON_ERROR(qmi8658_set_enabled(true, true), TAG, "enable");

    uint8_t rev = 0;
    (void)rd8(REG_REVISION, &rev);
    s_present = true;
    ESP_LOGI(TAG, "QMI8658 ready (rev 0x%02X)", rev);
    return ESP_OK;
}

bool qmi8658_is_present(void)
{
    return s_present;
}

esp_err_t qmi8658_set_enabled(bool accel, bool gyro)
{
    const uint8_t val = (uint8_t)((accel ? CTRL7_ACC_EN : 0) | (gyro ? CTRL7_GYR_EN : 0));
    return update_bits(REG_CTRL7, CTRL7_ACC_EN | CTRL7_GYR_EN, val);
}

esp_err_t qmi8658_read(qmi8658_data_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Temperature and the six axes are contiguous from 0x33, so one burst
     * read gets everything and guarantees a coherent sample set. */
    uint8_t raw[14];
    ESP_RETURN_ON_ERROR(rd(REG_TEMP_L, raw, sizeof(raw)), TAG, "burst read");

    const int16_t t  = (int16_t)((raw[1] << 8) | raw[0]);
    const int16_t ax = (int16_t)((raw[3] << 8) | raw[2]);
    const int16_t ay = (int16_t)((raw[5] << 8) | raw[4]);
    const int16_t az = (int16_t)((raw[7] << 8) | raw[6]);
    const int16_t gx = (int16_t)((raw[9] << 8) | raw[8]);
    const int16_t gy = (int16_t)((raw[11] << 8) | raw[10]);
    const int16_t gz = (int16_t)((raw[13] << 8) | raw[12]);

    out->ax = (float)ax * s_acc_scale;
    out->ay = (float)ay * s_acc_scale;
    out->az = (float)az * s_acc_scale;
    out->gx = (float)gx * s_gyr_scale;
    out->gy = (float)gy * s_gyr_scale;
    out->gz = (float)gz * s_gyr_scale;
    out->temp_c = (float)t / 256.0f;   /* Q8.8 degrees Celsius */
    return ESP_OK;
}

esp_err_t qmi8658_pedometer_enable(bool enable)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    if (enable) {
        /* Load the default pedometer tuning, then arm the engine. */
        ESP_RETURN_ON_ERROR(wr8(REG_CTRL9, CTRL9_CMD_CONFIGURE_PEDOMETER), TAG, "ped cfg");
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return update_bits(REG_CTRL8, CTRL8_PED_EN, enable ? CTRL8_PED_EN : 0);
}

esp_err_t qmi8658_read_steps(uint32_t *out_steps)
{
    if (out_steps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t b[3];
    ESP_RETURN_ON_ERROR(rd(REG_STEP_CNT_LOW, b, sizeof(b)), TAG, "step read");
    *out_steps = ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | b[0];
    return ESP_OK;
}

esp_err_t qmi8658_reset_steps(void)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    return wr8(REG_CTRL9, CTRL9_CMD_RESET_PEDOMETER);
}

esp_err_t qmi8658_enter_low_power(qmi8658_odr_t odr)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    /* The gyro is the expensive half - roughly 1.5 mA against the
     * accelerometer's few microamps. Turn it off first, then slow the
     * accelerometer down; the pedometer keeps running off it. */
    ESP_RETURN_ON_ERROR(qmi8658_set_enabled(true, false), TAG, "gyro off");
    return update_bits(REG_CTRL2, 0x0F, (uint8_t)(odr & 0x0F));
}

esp_err_t qmi8658_exit_low_power(void)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(apply_config(), TAG, "restore config");
    return qmi8658_set_enabled(true, true);
}
