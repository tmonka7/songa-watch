/*
 * Board facts for the Waveshare ESP32-S3-Touch-AMOLED-2.06.
 *
 * The display, touch, I2S codec and SD card are owned by the vendor BSP
 * (components/esp32_s3_touch_amoled_2_06). This header covers the parts the
 * BSP leaves alone: the I2C addresses of the PMU/IMU/RTC, and the pins left
 * free for an add-on camera.
 *
 * Pins claimed by the BSP - do not reuse:
 *   I2C    SCL 14, SDA 15
 *   QSPI   CS 12, PCLK 11, D0 4, D1 5, D2 6, D3 7, RST 8
 *   Touch  RST 9, INT 38
 *   I2S    MCLK 16, BCLK 41, WS 45, DOUT 40, DIN 42, PA_EN 46
 *   SDMMC  CLK 2, CMD 1, D0 3
 * Also unavailable: 0 (BOOT), 19/20 (USB), 26-37 (flash + octal PSRAM),
 * 43/44 (UART0 console).
 */
#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ I2C bus */
/* All four are on the BSP's I2C bus (SCL 14 / SDA 15), reachable through
 * bsp_i2c_get_handle(). */
#define WATCH_I2C_ADDR_TOUCH_FT3168   0x38
#define WATCH_I2C_ADDR_PMU_AXP2101    0x34
#define WATCH_I2C_ADDR_RTC_PCF85063   0x51
#define WATCH_I2C_ADDR_IMU_QMI8658    0x6B  /* SA0 high; 0x6A when pulled low */

/* ------------------------------------------------------------------ buttons */
#define WATCH_GPIO_BOOT_BTN           GPIO_NUM_0

/* The PWR button is wired to the AXP2101 PWRON pin, not to the SoC. Presses
 * arrive as AXP2101 IRQs (see axp2101_irq_read), never as a GPIO edge. */

/* ------------------------------------------------------------------ display */
#define WATCH_LCD_H_RES               410
#define WATCH_LCD_V_RES               502

#ifdef __cplusplus
}
#endif
