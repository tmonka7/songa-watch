/*
 * Motion sensing.
 *
 * Samples the QMI8658 on its own task and keeps a short history so the
 * Motion Graph screen can draw without the UI having to buffer anything.
 * Step counts come from the IMU's hardware pedometer, not from the samples,
 * so they keep accruing while the SoC sleeps.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How many points the graph screen shows. 15 s at the 4 Hz UI feed. */
#define SVC_SENSORS_HISTORY_LEN 60

typedef struct {
    float    ax, ay, az;    /* g */
    float    gx, gy, gz;    /* dps */
    float    temp_c;
    uint32_t steps;
    int64_t  timestamp_us;
} svc_sensors_sample_t;

/** @brief Start the IMU and its sampling task. */
esp_err_t svc_sensors_init(void);

/** @brief The most recent sample. */
const svc_sensors_sample_t *svc_sensors_latest(void);

/** @brief True when the IMU answered at init. */
bool svc_sensors_available(void);

/**
 * @brief Copy the rolling history, oldest first.
 *
 * @param[out] ax,ay,az,gx,gy,gz Arrays of SVC_SENSORS_HISTORY_LEN floats.
 *             Pass NULL for any axis you do not need.
 * @return Number of valid points, which is less than the buffer length until
 *         it has filled once.
 */
size_t svc_sensors_get_history(float *ax, float *ay, float *az,
                               float *gx, float *gy, float *gz);

/** @brief Today's step count. */
uint32_t svc_sensors_steps(void);

/** @brief Zero the step counter, in hardware and in the daily total. */
esp_err_t svc_sensors_reset_steps(void);

/**
 * @brief Slow the IMU down for sleep, or bring it back.
 *
 * svc_power drives this; call it yourself only if you are bypassing the
 * idle state machine.
 */
esp_err_t svc_sensors_set_low_power(bool low_power);

#ifdef __cplusplus
}
#endif
