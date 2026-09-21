/*
 * AXP2101 power management unit.
 *
 * Only the parts the watch needs: fuel gauge, charger state, charge limits,
 * the PWRON key IRQ and a real power-off. Rail voltages are left at the
 * values the board straps at reset - changing them on this board turns
 * things off that need to stay on.
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
    AXP2101_CHG_TRICKLE = 0,
    AXP2101_CHG_PRE     = 1,
    AXP2101_CHG_CC      = 2,
    AXP2101_CHG_CV      = 3,
    AXP2101_CHG_DONE    = 4,
    AXP2101_CHG_STOP    = 5,
} axp2101_chg_state_t;

/* Constant-current charge limit, REG 0x62 [4:0]. */
typedef enum {
    AXP2101_ICC_100MA = 4,
    AXP2101_ICC_200MA = 8,
    AXP2101_ICC_300MA = 9,
    AXP2101_ICC_400MA = 10,
    AXP2101_ICC_500MA = 11,
} axp2101_icc_t;

typedef struct {
    bool                vbus_present;   /* USB attached and in spec */
    bool                battery_present;
    bool                charging;
    uint8_t             percent;        /* 0-100, 255 when no battery */
    uint16_t            vbat_mv;
    uint16_t            vbus_mv;
    uint16_t            vsys_mv;
    int16_t             ibat_ma;        /* >0 charging, <0 discharging */
    float               die_temp_c;
    axp2101_chg_state_t chg_state;
} axp2101_status_t;

/* IRQ bits, as returned by axp2101_irq_read(). */
#define AXP2101_IRQ_PWRON_SHORT   (1u << 0)
#define AXP2101_IRQ_PWRON_LONG    (1u << 1)
#define AXP2101_IRQ_VBUS_INSERT   (1u << 2)
#define AXP2101_IRQ_VBUS_REMOVE   (1u << 3)
#define AXP2101_IRQ_BAT_INSERT    (1u << 4)
#define AXP2101_IRQ_BAT_REMOVE    (1u << 5)
#define AXP2101_IRQ_CHG_START     (1u << 6)
#define AXP2101_IRQ_CHG_DONE      (1u << 7)
#define AXP2101_IRQ_BAT_LOW       (1u << 8)

/**
 * @brief Probe the PMU on @p bus and configure charging + measurement.
 *
 * Safe to call once at boot. Leaves every rail exactly as the board strapped
 * it and only touches ADC, charger and IRQ configuration.
 */
esp_err_t axp2101_init(i2c_master_bus_handle_t bus);

/** @brief Read the whole power picture in one go. */
esp_err_t axp2101_read_status(axp2101_status_t *out);

/** @brief Read and clear the latched IRQ flags (AXP2101_IRQ_* bits). */
esp_err_t axp2101_irq_read(uint32_t *out_flags);

/** @brief Constant-current charge limit. Lower it to charge a small cell gently. */
esp_err_t axp2101_set_charge_current(axp2101_icc_t icc);

/**
 * @brief Cut every rail except VRTC. This is a real power-off: only a PWRON
 *        press or USB insertion brings the board back.
 */
esp_err_t axp2101_power_off(void);

/** @brief True once axp2101_init() has found the chip. */
bool axp2101_is_present(void);

#ifdef __cplusplus
}
#endif
