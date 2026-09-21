/*
 * PCF85063 real-time clock.
 *
 * The RTC runs off VRTC, which the AXP2101 keeps alive through a full
 * power-off. It is the only thing on the board that still knows the time
 * after the battery-backed rails go down, so the watch reads it at every
 * boot and writes it back whenever SNTP or the user changes the clock.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Probe the RTC and start its oscillator. */
esp_err_t pcf85063_init(i2c_master_bus_handle_t bus);

/**
 * @brief Read the RTC into a broken-down UTC time.
 *
 * @param[out] out       Populated on success. tm_isdst is set to 0.
 * @param[out] out_valid Optional. False when the oscillator-stop flag is set,
 *                       meaning the RTC lost power and the value is garbage.
 */
esp_err_t pcf85063_get_time(struct tm *out, bool *out_valid);

/** @brief Write a broken-down UTC time to the RTC and clear the stop flag. */
esp_err_t pcf85063_set_time(const struct tm *t);

/**
 * @brief Copy the RTC into the system clock.
 *
 * Does nothing and returns ESP_ERR_INVALID_STATE when the RTC reports that
 * it lost power, so a dead coin cell never drags the clock back to 2000.
 */
esp_err_t pcf85063_sync_to_system(void);

/** @brief Copy the system clock into the RTC. */
esp_err_t pcf85063_sync_from_system(void);

/** @brief True once pcf85063_init() has found the chip. */
bool pcf85063_is_present(void);

#ifdef __cplusplus
}
#endif
