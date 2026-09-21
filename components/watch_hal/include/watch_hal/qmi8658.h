/*
 * QMI8658 6-axis IMU (3-axis accel + 3-axis gyro).
 *
 * The chip has a hardware pedometer, which the watch leans on: the SoC can
 * sleep while the IMU keeps counting steps, and reads the total on wake
 * instead of running the CPU at 125 Hz.
 *
 * The chip also has a wake-on-motion block that asserts INT1/INT2, but this
 * board brings neither pin out to a GPIO, so nothing can be woken by it.
 * Raise-to-wake is therefore done by sampling this driver from the power
 * task while the watch sleeps - see svc_power.c.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QMI8658_ACC_2G  = 0,
    QMI8658_ACC_4G  = 1,
    QMI8658_ACC_8G  = 2,
    QMI8658_ACC_16G = 3,
} qmi8658_acc_range_t;

typedef enum {
    QMI8658_GYR_16DPS   = 0,
    QMI8658_GYR_32DPS   = 1,
    QMI8658_GYR_64DPS   = 2,
    QMI8658_GYR_128DPS  = 3,
    QMI8658_GYR_256DPS  = 4,
    QMI8658_GYR_512DPS  = 5,
    QMI8658_GYR_1024DPS = 6,
    QMI8658_GYR_2048DPS = 7,
} qmi8658_gyr_range_t;

/* Output data rate. The LOWPOWER rates only apply while the gyro is off. */
typedef enum {
    QMI8658_ODR_1000HZ        = 3,
    QMI8658_ODR_500HZ         = 4,
    QMI8658_ODR_250HZ         = 5,
    QMI8658_ODR_125HZ         = 6,
    QMI8658_ODR_62_5HZ        = 7,
    QMI8658_ODR_31_25HZ       = 8,
    QMI8658_ODR_LOWPOWER_128HZ = 12,
    QMI8658_ODR_LOWPOWER_21HZ  = 13,
    QMI8658_ODR_LOWPOWER_11HZ  = 14,
    QMI8658_ODR_LOWPOWER_3HZ   = 15,
} qmi8658_odr_t;

typedef struct {
    float ax, ay, az;   /* g */
    float gx, gy, gz;   /* degrees per second */
    float temp_c;
} qmi8658_data_t;

typedef struct {
    qmi8658_acc_range_t acc_range;
    qmi8658_gyr_range_t gyr_range;
    qmi8658_odr_t       acc_odr;
    qmi8658_odr_t       gyr_odr;
} qmi8658_config_t;

#define QMI8658_DEFAULT_CONFIG() (qmi8658_config_t){ \
    .acc_range = QMI8658_ACC_8G,                     \
    .gyr_range = QMI8658_GYR_512DPS,                 \
    .acc_odr   = QMI8658_ODR_125HZ,                  \
    .gyr_odr   = QMI8658_ODR_125HZ,                  \
}

/** @brief Probe, soft-reset and configure the IMU on @p bus. */
esp_err_t qmi8658_init(i2c_master_bus_handle_t bus, const qmi8658_config_t *cfg);

/** @brief Read accel, gyro and die temperature as engineering units. */
esp_err_t qmi8658_read(qmi8658_data_t *out);

/**
 * @brief Read just the three accelerometer axes, in g.
 *
 * A 6-byte transfer rather than the 14 qmi8658_read() needs. Meant for the
 * sleep path, which samples often and cares only about the gravity vector.
 */
esp_err_t qmi8658_read_accel(float *ax_g, float *ay_g, float *az_g);

/** @brief Turn the accelerometer and/or gyroscope on or off. */
esp_err_t qmi8658_set_enabled(bool accel, bool gyro);

/**
 * @brief Enable the on-chip pedometer.
 *
 * Steps keep accumulating while the SoC is asleep, which is the whole point:
 * the watch reads the total on wake instead of running the CPU at 125 Hz.
 */
esp_err_t qmi8658_pedometer_enable(bool enable);

/** @brief Read the hardware step total (24-bit, wraps at 16777215). */
esp_err_t qmi8658_read_steps(uint32_t *out_steps);

/** @brief Zero the hardware step total. */
esp_err_t qmi8658_reset_steps(void);

/**
 * @brief Drop into the accel-only low-power mode used while the watch sleeps.
 *
 * Gyro off, accelerometer at @p odr. Current draw falls to a few microamps
 * at the 3 Hz setting.
 */
esp_err_t qmi8658_enter_low_power(qmi8658_odr_t odr);

/** @brief Restore the configuration passed to qmi8658_init(). */
esp_err_t qmi8658_exit_low_power(void);

/** @brief True once qmi8658_init() has found the chip. */
bool qmi8658_is_present(void);

#ifdef __cplusplus
}
#endif
