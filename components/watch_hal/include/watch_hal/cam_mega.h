/*
 * Arducam Mega SPI camera (3 MP / 5 MP).
 *
 * Why this part: the ESP32-S3-Touch-AMOLED-2.06 has no camera and no room
 * for one on a DVP bus - after the QSPI panel, I2S codec, SDMMC card and
 * octal PSRAM there are eight GPIOs left, and a parallel sensor needs
 * thirteen. The Mega takes four wires and hands back finished JPEG frames,
 * so it is the only shape of camera this board can actually carry.
 *
 * Wiring (defaults, all Kconfig-adjustable under "Songa Watch hardware"):
 *   SCK  GPIO10      MOSI GPIO13
 *   MISO GPIO18      CS   GPIO21
 *   VCC  3V3         GND  GND
 *
 * The module is JPEG-out, so frames land as compressed buffers. Decoding to
 * something LVGL can draw is svc_camera's job, not this driver's.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAM_MEGA_RES_QQVGA   = 0x00,  /* 160x120  */
    CAM_MEGA_RES_QVGA    = 0x01,  /* 320x240  */
    CAM_MEGA_RES_VGA     = 0x02,  /* 640x480  */
    CAM_MEGA_RES_SVGA    = 0x03,  /* 800x600  */
    CAM_MEGA_RES_HD      = 0x04,  /* 1280x720 */
    CAM_MEGA_RES_UXGA    = 0x06,  /* 1600x1200 */
    CAM_MEGA_RES_FHD     = 0x07,  /* 1920x1080 */
    CAM_MEGA_RES_320X320 = 0x0C,  /* square, handy for the detector */
} cam_mega_res_t;

typedef enum {
    CAM_MEGA_FMT_JPEG   = 0x01,
    CAM_MEGA_FMT_RGB565 = 0x02,
    CAM_MEGA_FMT_YUV    = 0x03,
} cam_mega_fmt_t;

typedef struct {
    char     model[8];        /* "3MP" or "5MP" */
    uint8_t  sensor_id;
    uint8_t  fw_year;
    uint8_t  fw_month;
    uint8_t  fw_day;
    uint8_t  fpga_version;
    bool     supports_focus;
} cam_mega_info_t;

/**
 * @brief Bring up the SPI bus and identify the module.
 *
 * @return ESP_ERR_NOT_FOUND when nothing answers on the bus - the usual
 *         result when no module is plugged in, and not a fatal error.
 */
esp_err_t cam_mega_init(void);

/** @brief Release the SPI bus and forget the module. */
esp_err_t cam_mega_deinit(void);

/** @brief True once cam_mega_init() has identified a module. */
bool cam_mega_is_present(void);

/** @brief What the module reported at init. */
const cam_mega_info_t *cam_mega_get_info(void);

/**
 * @brief Take one picture and leave it in the module's FIFO.
 *
 * Blocks until the sensor signals capture-done, then reports how many bytes
 * are waiting. Read them with cam_mega_read().
 *
 * @param[out] out_len Bytes available in the FIFO.
 */
esp_err_t cam_mega_capture(cam_mega_res_t res, cam_mega_fmt_t fmt, uint32_t *out_len);

/**
 * @brief Drain bytes from the FIFO into @p buf.
 *
 * Call repeatedly until it returns 0. The first call after a capture opens a
 * burst read; the driver handles the leading dummy byte the module emits.
 *
 * @return Bytes actually copied, which may be fewer than @p len at the end
 *         of the frame.
 */
uint32_t cam_mega_read(uint8_t *buf, uint32_t len);

/** @brief Abandon whatever is still in the FIFO. */
esp_err_t cam_mega_flush(void);

/** @brief JPEG quality, 0 = high, 1 = default, 2 = low. */
esp_err_t cam_mega_set_quality(uint8_t quality);

/** @brief Brightness, -3..+3. */
esp_err_t cam_mega_set_brightness(int8_t level);

/** @brief Auto-focus, 5 MP modules only. Ignored elsewhere. */
esp_err_t cam_mega_set_autofocus(bool enable);

/** @brief Put the sensor into its low-power state (or take it out). */
esp_err_t cam_mega_set_low_power(bool enable);

#ifdef __cplusplus
}
#endif
