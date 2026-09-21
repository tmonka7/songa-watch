#include "watch_hal/cam_mega.h"

#include <string.h>
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_WATCH_CAMERA_ENABLE

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

static const char *TAG = "cam_mega";

/* ------------------------------------------------- Arducam Mega registers */
#define ARDUCHIP_FIFO        0x04
#define FIFO_CLEAR_ID_MASK   0x01
#define FIFO_START_MASK      0x02

#define ARDUCHIP_TRIG        0x44
#define CAP_DONE_MASK        0x04

#define FIFO_SIZE1           0x45
#define FIFO_SIZE2           0x46
#define FIFO_SIZE3           0x47
#define BURST_FIFO_READ      0x3C

#define CAM_REG_POWER_CONTROL       0x02
#define CAM_REG_SENSOR_RESET        0x07
#define CAM_REG_DEBUG_DEVICE_ADDR   0x0A
#define CAM_REG_FORMAT              0x20
#define CAM_REG_CAPTURE_RESOLUTION  0x21
#define CAM_REG_BRIGHTNESS_CONTROL  0x22
#define CAM_REG_AUTO_FOCUS_CONTROL  0x29
#define CAM_REG_IMAGE_QUALITY       0x2A
#define CAM_REG_SENSOR_ID           0x40
#define CAM_REG_YEAR_ID             0x41
#define CAM_REG_MONTH_ID            0x42
#define CAM_REG_DAY_ID              0x43
#define CAM_REG_SENSOR_STATE        0x44
#define CAM_REG_FPGA_VERSION        0x49

#define CAM_SENSOR_RESET_ENABLE     (1u << 6)
#define CAM_SET_CAPTURE_MODE        (0u << 7)
#define CAM_REG_SENSOR_STATE_IDLE   (1u << 1)

/* Sensor IDs reported in CAM_REG_SENSOR_ID. */
#define SENSOR_3MP_1  0x81
#define SENSOR_3MP_2  0x82
#define SENSOR_5MP_1  0x81
#define SENSOR_5MP_2  0x83

#define MEGA_I2C_DEVICE_ADDRESS 0x78

#define CAPTURE_TIMEOUT_MS  3000
#define I2C_IDLE_TIMEOUT_MS 1000

static spi_device_handle_t s_spi;
static bool                s_present;
static cam_mega_info_t     s_info;
static uint32_t            s_remaining;      /* bytes left in the FIFO */
static bool                s_burst_started;
static uint8_t             s_cur_fmt = 0xFF;
static uint8_t             s_cur_res = 0xFF;

/* ------------------------------------------------------------ SPI plumbing */

/* The Mega drives CS itself through the driver's pre/post callbacks, but a
 * burst FIFO read has to hold CS low across many transactions. Keeping CS as
 * a plain GPIO is the only way to do both, so the SPI device is configured
 * with no CS pin and we toggle it here. */
static void cs_low(void)
{
    gpio_set_level((gpio_num_t)CONFIG_WATCH_CAMERA_PIN_CS, 0);
}

static void cs_high(void)
{
    gpio_set_level((gpio_num_t)CONFIG_WATCH_CAMERA_PIN_CS, 1);
}

static esp_err_t spi_xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (len == 0) {
        return ESP_OK;
    }
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_polling_transmit(s_spi, &t);
}

/* A register write is address|0x80 then the value, framed by CS. */
static esp_err_t bus_write(uint8_t addr, uint8_t value)
{
    const uint8_t tx[2] = { (uint8_t)(addr | 0x80), value };
    cs_low();
    const esp_err_t err = spi_xfer(tx, NULL, sizeof(tx));
    cs_high();
    /* The module needs a moment to latch a register write. */
    esp_rom_delay_us(1000);
    return err;
}

/* A register read is address (bit7 clear), then two bytes out; the module
 * only puts real data on the second one. */
static esp_err_t bus_read(uint8_t addr, uint8_t *out)
{
    const uint8_t tx[3] = { (uint8_t)(addr & 0x7F), 0x00, 0x00 };
    uint8_t rx[3] = { 0 };
    cs_low();
    const esp_err_t err = spi_xfer(tx, rx, sizeof(tx));
    cs_high();
    if (err == ESP_OK && out != NULL) {
        *out = rx[2];
    }
    return err;
}

/* Every register write goes through the module's own I2C master to the
 * sensor, so we must wait for that to drain before touching it again. */
static esp_err_t wait_i2c_idle(void)
{
    const int64_t deadline = esp_timer_get_time() + (I2C_IDLE_TIMEOUT_MS * 1000);
    while (esp_timer_get_time() < deadline) {
        uint8_t state = 0;
        if (bus_read(CAM_REG_SENSOR_STATE, &state) == ESP_OK &&
            (state & 0x03) == CAM_REG_SENSOR_STATE_IDLE) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    ESP_LOGW(TAG, "sensor did not return to idle");
    return ESP_ERR_TIMEOUT;
}

static esp_err_t write_reg_sync(uint8_t addr, uint8_t value)
{
    ESP_RETURN_ON_ERROR(bus_write(addr, value), TAG, "write 0x%02X", addr);
    return wait_i2c_idle();
}

/* ---------------------------------------------------------------------- api */

esp_err_t cam_mega_init(void)
{
    if (s_present) {
        return ESP_OK;
    }

    const gpio_config_t cs_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_WATCH_CAMERA_PIN_CS,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cs_cfg), TAG, "cs gpio");
    cs_high();

    const spi_bus_config_t bus_cfg = {
        .sclk_io_num     = CONFIG_WATCH_CAMERA_PIN_SCK,
        .mosi_io_num     = CONFIG_WATCH_CAMERA_PIN_MOSI,
        .miso_io_num     = CONFIG_WATCH_CAMERA_PIN_MISO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4096,
    };
    const spi_host_device_t host = (spi_host_device_t)CONFIG_WATCH_CAMERA_SPI_HOST;
    esp_err_t err = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    const spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = CONFIG_WATCH_CAMERA_SPI_FREQ_KHZ * 1000,
        .mode           = 0,
        .spics_io_num   = -1,          /* CS driven by hand, see cs_low() */
        .queue_size     = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(host, &dev_cfg, &s_spi), TAG, "spi add device");

    /* Reset the CPLD and sensor, then read the identity block. */
    if (bus_write(CAM_REG_SENSOR_RESET, CAM_SENSOR_RESET_ENABLE) != ESP_OK ||
        wait_i2c_idle() != ESP_OK) {
        ESP_LOGW(TAG, "no Arducam Mega responding - camera features stay off");
        spi_bus_remove_device(s_spi);
        s_spi = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    memset(&s_info, 0, sizeof(s_info));
    uint8_t v = 0;
    if (bus_read(CAM_REG_SENSOR_ID, &v) != ESP_OK || v == 0x00 || v == 0xFF) {
        ESP_LOGW(TAG, "no Arducam Mega responding (sensor id 0x%02X)", v);
        spi_bus_remove_device(s_spi);
        s_spi = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    s_info.sensor_id = v;
    (void)wait_i2c_idle();

    if (bus_read(CAM_REG_YEAR_ID, &v) == ESP_OK)      { s_info.fw_year  = v & 0x3F; }
    (void)wait_i2c_idle();
    if (bus_read(CAM_REG_MONTH_ID, &v) == ESP_OK)     { s_info.fw_month = v & 0x0F; }
    (void)wait_i2c_idle();
    if (bus_read(CAM_REG_DAY_ID, &v) == ESP_OK)       { s_info.fw_day   = v & 0x1F; }
    (void)wait_i2c_idle();
    if (bus_read(CAM_REG_FPGA_VERSION, &v) == ESP_OK) { s_info.fpga_version = v; }
    (void)wait_i2c_idle();

    /* SENSOR_5MP_2 is the only id unique to a family; the rest overlap, so
     * treat anything else as the 3 MP part, which has the smaller feature
     * set and therefore the safer defaults. */
    if (s_info.sensor_id == SENSOR_5MP_2) {
        strncpy(s_info.model, "5MP", sizeof(s_info.model) - 1);
        s_info.supports_focus = true;
    } else {
        strncpy(s_info.model, "3MP", sizeof(s_info.model) - 1);
        s_info.supports_focus = false;
    }

    (void)write_reg_sync(CAM_REG_DEBUG_DEVICE_ADDR, MEGA_I2C_DEVICE_ADDRESS);

    s_present = true;
    ESP_LOGI(TAG, "Arducam Mega %s found (id 0x%02X, fw 20%02u-%02u-%02u, fpga %u)",
             s_info.model, s_info.sensor_id, s_info.fw_year, s_info.fw_month,
             s_info.fw_day, s_info.fpga_version);
    return ESP_OK;
}

esp_err_t cam_mega_deinit(void)
{
    if (!s_present) {
        return ESP_OK;
    }
    spi_bus_remove_device(s_spi);
    spi_bus_free((spi_host_device_t)CONFIG_WATCH_CAMERA_SPI_HOST);
    s_spi = NULL;
    s_present = false;
    s_remaining = 0;
    s_cur_fmt = 0xFF;
    s_cur_res = 0xFF;
    return ESP_OK;
}

bool cam_mega_is_present(void)
{
    return s_present;
}

const cam_mega_info_t *cam_mega_get_info(void)
{
    return s_present ? &s_info : NULL;
}

esp_err_t cam_mega_capture(cam_mega_res_t res, cam_mega_fmt_t fmt, uint32_t *out_len)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }

    /* Only reprogram what changed - each of these costs an I2C round trip
     * inside the module and a visible stall in the preview. */
    if (s_cur_fmt != (uint8_t)fmt) {
        ESP_RETURN_ON_ERROR(write_reg_sync(CAM_REG_FORMAT, (uint8_t)fmt), TAG, "format");
        s_cur_fmt = (uint8_t)fmt;
    }
    if (s_cur_res != (uint8_t)res) {
        ESP_RETURN_ON_ERROR(write_reg_sync(CAM_REG_CAPTURE_RESOLUTION,
                                           (uint8_t)(CAM_SET_CAPTURE_MODE | res)),
                            TAG, "resolution");
        s_cur_res = (uint8_t)res;
    }

    ESP_RETURN_ON_ERROR(bus_write(ARDUCHIP_FIFO, FIFO_CLEAR_ID_MASK), TAG, "fifo clear");
    ESP_RETURN_ON_ERROR(bus_write(ARDUCHIP_FIFO, FIFO_START_MASK), TAG, "fifo start");

    const int64_t deadline = esp_timer_get_time() + (CAPTURE_TIMEOUT_MS * 1000);
    for (;;) {
        uint8_t trig = 0;
        ESP_RETURN_ON_ERROR(bus_read(ARDUCHIP_TRIG, &trig), TAG, "trig");
        if (trig & CAP_DONE_MASK) {
            break;
        }
        if (esp_timer_get_time() > deadline) {
            ESP_LOGW(TAG, "capture timed out");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    uint8_t l1 = 0, l2 = 0, l3 = 0;
    ESP_RETURN_ON_ERROR(bus_read(FIFO_SIZE1, &l1), TAG, "len1");
    ESP_RETURN_ON_ERROR(bus_read(FIFO_SIZE2, &l2), TAG, "len2");
    ESP_RETURN_ON_ERROR(bus_read(FIFO_SIZE3, &l3), TAG, "len3");

    s_remaining = (((uint32_t)l3 << 16) | ((uint32_t)l2 << 8) | l1) & 0xFFFFFF;
    s_burst_started = false;

    if (out_len != NULL) {
        *out_len = s_remaining;
    }
    return (s_remaining > 0) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

uint32_t cam_mega_read(uint8_t *buf, uint32_t len)
{
    if (!s_present || buf == NULL || len == 0 || s_remaining == 0) {
        return 0;
    }
    if (len > s_remaining) {
        len = s_remaining;
    }

    cs_low();
    const uint8_t cmd = BURST_FIFO_READ;
    if (spi_xfer(&cmd, NULL, 1) != ESP_OK) {
        cs_high();
        return 0;
    }
    if (!s_burst_started) {
        /* The module emits one dummy byte before the first real one. */
        const uint8_t dummy = 0x00;
        if (spi_xfer(&dummy, NULL, 1) != ESP_OK) {
            cs_high();
            return 0;
        }
        s_burst_started = true;
    }

    /* Read-only half of the burst: MOSI idles, MISO carries the frame. */
    const esp_err_t err = spi_xfer(NULL, buf, len);
    cs_high();

    if (err != ESP_OK) {
        return 0;
    }
    s_remaining -= len;
    return len;
}

esp_err_t cam_mega_flush(void)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    s_remaining = 0;
    s_burst_started = false;
    return bus_write(ARDUCHIP_FIFO, FIFO_CLEAR_ID_MASK);
}

esp_err_t cam_mega_set_quality(uint8_t quality)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    if (quality > 2) {
        return ESP_ERR_INVALID_ARG;
    }
    return write_reg_sync(CAM_REG_IMAGE_QUALITY, quality);
}

esp_err_t cam_mega_set_brightness(int8_t level)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    if (level < -3 || level > 3) {
        return ESP_ERR_INVALID_ARG;
    }
    /* The module encodes negative steps as even codes and positive as odd:
     * 0 = default, 1/3/5 = +1/+2/+3, 2/4/6 = -1/-2/-3. */
    const uint8_t code = (level == 0) ? 0
                       : (level > 0)  ? (uint8_t)(level * 2 - 1)
                                      : (uint8_t)(-level * 2);
    return write_reg_sync(CAM_REG_BRIGHTNESS_CONTROL, code);
}

esp_err_t cam_mega_set_autofocus(bool enable)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_info.supports_focus) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return write_reg_sync(CAM_REG_AUTO_FOCUS_CONTROL, enable ? 0x01 : 0x00);
}

esp_err_t cam_mega_set_low_power(bool enable)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 0x07 parks the sensor, 0x05 wakes it. */
    return write_reg_sync(CAM_REG_POWER_CONTROL, enable ? 0x07 : 0x05);
}

#else  /* !CONFIG_WATCH_CAMERA_ENABLE */

/* Camera compiled out. The UI still calls these; they report "no camera"
 * so the Camera and AI Vision screens can say so plainly. */

esp_err_t cam_mega_init(void)                   { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cam_mega_deinit(void)                 { return ESP_OK; }
bool cam_mega_is_present(void)                  { return false; }
const cam_mega_info_t *cam_mega_get_info(void)  { return NULL; }
esp_err_t cam_mega_capture(cam_mega_res_t res, cam_mega_fmt_t fmt, uint32_t *out_len)
{
    (void)res; (void)fmt;
    if (out_len != NULL) { *out_len = 0; }
    return ESP_ERR_NOT_SUPPORTED;
}
uint32_t cam_mega_read(uint8_t *buf, uint32_t len)   { (void)buf; (void)len; return 0; }
esp_err_t cam_mega_flush(void)                       { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cam_mega_set_quality(uint8_t q)            { (void)q; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cam_mega_set_brightness(int8_t l)          { (void)l; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cam_mega_set_autofocus(bool e)             { (void)e; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cam_mega_set_low_power(bool e)             { (void)e; return ESP_ERR_NOT_SUPPORTED; }

#endif /* CONFIG_WATCH_CAMERA_ENABLE */
