/*
 * Power and idle management.
 *
 * This is where the battery budget is actually spent or saved. The policy:
 *
 *   active  - full brightness, IMU at 125 Hz, LVGL running
 *   dimmed  - brightness cut to a quarter after idle_dim_sec of no touch
 *   asleep  - panel off (AMOLED 0x51 = 0, so the pixels draw nothing), LVGL
 *             task stopped, IMU dropped to 3 Hz accel-only with its hardware
 *             pedometer still counting, radios idled, and the SoC left to
 *             ESP-IDF automatic light sleep
 *
 * Waking is driven by the touch INT line and the PMU's power-key IRQ, so
 * nothing polls while the watch is idle.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "watch_hal/axp2101.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WATCH_POWER_ACTIVE = 0,
    WATCH_POWER_DIMMED,
    WATCH_POWER_ASLEEP,
} watch_power_state_t;

/**
 * @brief Start power management.
 *
 * Brings up the PMU, applies the ESP-IDF DFS/light-sleep policy and starts
 * the idle timer. Call after svc_settings_init().
 */
esp_err_t svc_power_init(void);

/** @brief The most recent PMU reading. Refreshed every few seconds. */
const axp2101_status_t *svc_power_status(void);

/** @brief Where the idle state machine currently is. */
watch_power_state_t svc_power_state(void);

/**
 * @brief Report user activity, restarting the idle countdown.
 *
 * The UI calls this on every touch. Calling it from a background task
 * defeats the whole idle policy - don't.
 */
void svc_power_notify_activity(void);

/** @brief Force the display awake, as a notification would. */
void svc_power_wake(void);

/** @brief Put the display to sleep now, without waiting for the timer. */
void svc_power_sleep(void);

/** @brief Apply a brightness in percent and remember it in settings. */
esp_err_t svc_power_set_brightness(uint8_t percent);

/** @brief The brightness actually on the panel right now (dimming included). */
uint8_t svc_power_get_brightness(void);

/**
 * @brief Hold the CPU at full speed for a latency-sensitive stretch.
 *
 * Takes an ESP-IDF PM lock. Every acquire needs a matching release, or the
 * watch never light-sleeps again.
 */
void svc_power_perf_lock_acquire(void);
void svc_power_perf_lock_release(void);

/** @brief Cut all rails via the PMU. Does not return if the PMU is present. */
void svc_power_shutdown(void);

/** @brief Reboot. */
void svc_power_restart(void);

#ifdef __cplusplus
}
#endif
