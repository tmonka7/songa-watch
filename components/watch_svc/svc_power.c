#include "watch_svc/svc_power.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_settings.h"
#include "watch_svc/svc_sensors.h"
#include "watch_hal/watch_board.h"
#include "watch_hal/qmi8658.h"

#include <math.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"

static const char *TAG = "svc_power";

#define PMU_POLL_PERIOD_MS   2000
#define IDLE_TICK_MS          500
/*
 * While asleep the idle loop runs faster, because it is the only thing
 * watching the touch panel.
 *
 * lvgl_port_stop() disables LVGL's timers, so LVGL no longer reads the
 * touch input device - a tap would never reach a widget. The touch INT line
 * is armed as a light-sleep wake source, so the SoC does come back the
 * instant a finger lands; this loop is what notices and turns the screen on
 * again. 100 ms is short enough to catch a deliberate tap and long enough
 * that the wake overhead stays well under a milliamp on average.
 */
#define SLEEP_TICK_MS         100
#define DIM_BRIGHTNESS_DIV      4    /* dimmed = setting / 4 */
#define ALWAYS_ON_BRIGHTNESS    5    /* percent, for the always-on clock */

static axp2101_status_t    s_status;
static watch_power_state_t s_state = WATCH_POWER_ACTIVE;
static uint8_t             s_applied_brightness;
static int64_t             s_last_activity_us;
static esp_pm_lock_handle_t s_perf_lock;
static int                 s_perf_refs;
static bool                s_low_battery_warned;
static portMUX_TYPE        s_activity_mux = portMUX_INITIALIZER_UNLOCKED;

/* ----------------------------------------------------------- brightness */

static void apply_brightness(uint8_t percent)
{
    if (percent == s_applied_brightness) {
        return;
    }
    /* On this AMOLED there is no backlight pin - the BSP writes the panel's
     * 0x51 brightness register over the same QSPI bus the LVGL flush uses,
     * so the write has to be serialised against the LVGL task. The port's
     * mutex is recursive, which makes this safe to call from a UI callback
     * that already holds it. */
    if (!bsp_display_lock(200)) {
        ESP_LOGD(TAG, "display busy, deferring brightness change");
        return;   /* the idle tick will try again */
    }
    bsp_display_brightness_set(percent);
    bsp_display_unlock();
    s_applied_brightness = percent;
}

uint8_t svc_power_get_brightness(void)
{
    return s_applied_brightness;
}

esp_err_t svc_power_set_brightness(uint8_t percent)
{
    if (percent < 1)   { percent = 1; }
    if (percent > 100) { percent = 100; }
    const esp_err_t err = svc_settings_set_brightness(percent);
    if (s_state == WATCH_POWER_ACTIVE) {
        apply_brightness(percent);
    }
    return err;
}


/* ------------------------------------------------------- raise to wake */

/*
 * Lifting the wrist lights the screen.
 *
 * The QMI8658's own wake-on-motion block would be the cheap way to do this,
 * but it signals on INT1/INT2 and this board brings neither pin out to a
 * GPIO (BSP_CAPS_IMU is 0, and the BSP defines no IMU interrupt). So the
 * gesture is sampled here instead, on the sleep tick that already runs for
 * the touch panel - the accelerometer read is 6 bytes of I2C on a loop that
 * was going to wake anyway, which is why this costs so little.
 *
 * Axis convention: az is gravity's component along the display normal,
 * +1 g with the face toward the sky and -1 g with it toward the ground.
 * If your board mounts the IMU the other way up, set WATCH_RAISE_INVERT.
 *
 * The gesture is deliberately a sequence, not a threshold, because a
 * threshold alone fires in a pocket and on every roll in bed:
 *
 *   1. the face must first be away from the wearer  (az <= LOW)
 *   2. then come up to facing them  (az >= HIGH) within WINDOW_MS
 *   3. and hold there, nearly still, for HOLD_MS
 *
 * Step 3 is what separates looking at the watch from swinging an arm while
 * walking: an arm in motion never settles at 1 g.
 */

#define RAISE_LOW_TILT   (CONFIG_WATCH_RAISE_LOW_TILT_MG / 1000.0f)
#define RAISE_HIGH_TILT  (CONFIG_WATCH_RAISE_HIGH_TILT_MG / 1000.0f)
#define RAISE_STILL_BAND 0.25f   /* g away from 1 g still counts as settled */

static bool    s_raise_armed;
static int64_t s_raise_armed_us;
static int64_t s_raise_held_since_us;

static void raise_reset(void)
{
    s_raise_armed = false;
    s_raise_held_since_us = 0;
}

static bool raise_detect(int64_t now_us)
{
    float ax, ay, az;
    if (qmi8658_read_accel(&ax, &ay, &az) != ESP_OK) {
        return false;
    }
#if CONFIG_WATCH_RAISE_INVERT
    az = -az;
#endif

    if (az <= RAISE_LOW_TILT) {
        s_raise_armed = true;
        s_raise_armed_us = now_us;
        s_raise_held_since_us = 0;
        return false;
    }

    if (!s_raise_armed) {
        return false;
    }
    if ((now_us - s_raise_armed_us) > (CONFIG_WATCH_RAISE_WINDOW_MS * 1000LL)) {
        /* Too slow to be a raise - the watch was just carried around. */
        raise_reset();
        return false;
    }
    if (az < RAISE_HIGH_TILT) {
        s_raise_held_since_us = 0;
        return false;
    }

    /* Facing the wearer. Require it to be near-stationary, so a swinging
     * arm that happens to pass through the right angle does not count. */
    const float mag = sqrtf(ax * ax + ay * ay + az * az);
    if (mag < (1.0f - RAISE_STILL_BAND) || mag > (1.0f + RAISE_STILL_BAND)) {
        s_raise_held_since_us = 0;
        return false;
    }

    if (s_raise_held_since_us == 0) {
        s_raise_held_since_us = now_us;
        return false;
    }
    if ((now_us - s_raise_held_since_us) >= (CONFIG_WATCH_RAISE_HOLD_MS * 1000LL)) {
        raise_reset();
        return true;
    }
    return false;
}
/* --------------------------------------------------------- sleep / wake */

static void enter_state(watch_power_state_t next)
{
    if (next == s_state) {
        return;
    }
    const watch_settings_t *cfg = svc_settings_get();

    switch (next) {
    case WATCH_POWER_ACTIVE:
        if (s_state == WATCH_POWER_ASLEEP) {
            /* Order matters: get LVGL running again before the panel lights
             * up, or the first frame shown is whatever was left in the
             * buffer from before the sleep. */
            lvgl_port_resume();
            svc_sensors_set_low_power(false);
            raise_reset();
        }
        apply_brightness(cfg->brightness);
        svc_event_post(WATCH_EV_DISPLAY_WAKE, NULL, 0);
        break;

    case WATCH_POWER_DIMMED: {
        uint8_t dim = (uint8_t)(cfg->brightness / DIM_BRIGHTNESS_DIV);
        if (dim < 1) { dim = 1; }
        apply_brightness(dim);
        break;
    }

    case WATCH_POWER_ASLEEP:
        if (cfg->always_on) {
            /* Keep a dim clock rather than a black screen. LVGL stays up,
             * so this costs real current - it is a deliberate trade the
             * user opted into. */
            apply_brightness(ALWAYS_ON_BRIGHTNESS);
        } else {
            apply_brightness(0);
            lvgl_port_stop();
        }
        svc_sensors_set_low_power(true);
        /* Start the gesture from a clean slate: whatever the wrist was doing
         * on the way into sleep is not the raise we are looking for. */
        raise_reset();
        svc_event_post(WATCH_EV_DISPLAY_SLEEP, NULL, 0);
        break;
    }

    ESP_LOGD(TAG, "power state %d -> %d", (int)s_state, (int)next);
    s_state = next;
}

void svc_power_notify_activity(void)
{
    portENTER_CRITICAL(&s_activity_mux);
    s_last_activity_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_activity_mux);

    if (s_state != WATCH_POWER_ACTIVE) {
        enter_state(WATCH_POWER_ACTIVE);
    }
}

void svc_power_wake(void)
{
    svc_power_notify_activity();
}

void svc_power_sleep(void)
{
    enter_state(WATCH_POWER_ASLEEP);
}

watch_power_state_t svc_power_state(void)
{
    return s_state;
}

const axp2101_status_t *svc_power_status(void)
{
    return &s_status;
}

/* ------------------------------------------------------------- pm locks */

void svc_power_perf_lock_acquire(void)
{
    if (s_perf_lock == NULL) {
        return;
    }
    if (s_perf_refs++ == 0) {
        esp_pm_lock_acquire(s_perf_lock);
    }
}

void svc_power_perf_lock_release(void)
{
    if (s_perf_lock == NULL || s_perf_refs == 0) {
        return;
    }
    if (--s_perf_refs == 0) {
        esp_pm_lock_release(s_perf_lock);
    }
}

/* ------------------------------------------------------------- shutdown */

void svc_power_shutdown(void)
{
    ESP_LOGW(TAG, "shutting down");
    apply_brightness(0);
    lvgl_port_stop();
    vTaskDelay(pdMS_TO_TICKS(100));

    if (axp2101_is_present()) {
        axp2101_power_off();
        /* The PMU cuts the rails within a couple of hundred ms. If we are
         * still running after that, it did not take - fall through. */
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGE(TAG, "PMU did not cut power; falling back to deep sleep");
    }

    /* No PMU, or it refused: deep sleep is the next best thing. ext0 needs
     * an RTC-capable pin, which on the ESP32-S3 means GPIO0-21 - the touch
     * INT on GPIO38 does not qualify, so wake on the BOOT button instead. */
    esp_sleep_enable_ext0_wakeup(WATCH_GPIO_BOOT_BTN, 0);
    esp_deep_sleep_start();
}

void svc_power_restart(void)
{
    ESP_LOGW(TAG, "restarting");
    apply_brightness(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

/* ------------------------------------------------------------- pmu task */

static void handle_pmu_irq_flags(uint32_t flags)
{
    if (flags & AXP2101_IRQ_PWRON_SHORT) {
        ESP_LOGI(TAG, "power key: short press");
        if (s_state == WATCH_POWER_ASLEEP) {
            svc_power_wake();
        } else {
            svc_power_sleep();
        }
    }
    if (flags & AXP2101_IRQ_PWRON_LONG) {
        ESP_LOGI(TAG, "power key: long press -> shutdown");
        svc_power_shutdown();
    }
    if (flags & (AXP2101_IRQ_VBUS_INSERT | AXP2101_IRQ_VBUS_REMOVE)) {
        svc_power_wake();
    }
    if (flags & AXP2101_IRQ_BAT_LOW) {
        const uint8_t pct = s_status.percent;
        svc_event_post(WATCH_EV_LOW_BATTERY, &pct, sizeof(pct));
    }
}

static void power_task(void *arg)
{
    (void)arg;
    int64_t next_pmu_us = 0;

    for (;;) {
        const int64_t now = esp_timer_get_time();

        /* --- PMU polling ------------------------------------------------ */
        if (now >= next_pmu_us && axp2101_is_present()) {
            next_pmu_us = now + (PMU_POLL_PERIOD_MS * 1000);

            axp2101_status_t fresh;
            if (axp2101_read_status(&fresh) == ESP_OK) {
                const bool changed =
                    fresh.percent      != s_status.percent ||
                    fresh.charging     != s_status.charging ||
                    fresh.vbus_present != s_status.vbus_present ||
                    fresh.chg_state    != s_status.chg_state;
                s_status = fresh;
                if (changed) {
                    svc_event_post(WATCH_EV_POWER_CHANGED, &s_status, sizeof(s_status));
                }

                if (s_status.battery_present && !s_status.charging &&
                    s_status.percent <= CONFIG_WATCH_LOW_BATTERY_PCT) {
                    if (!s_low_battery_warned) {
                        s_low_battery_warned = true;
                        const uint8_t pct = s_status.percent;
                        svc_event_post(WATCH_EV_LOW_BATTERY, &pct, sizeof(pct));
                    }
                } else if (s_status.percent > CONFIG_WATCH_LOW_BATTERY_PCT + 5) {
                    /* Hysteresis, so a reading hovering on the threshold
                     * does not warn once every poll. */
                    s_low_battery_warned = false;
                }
            }

            uint32_t irq = 0;
            if (axp2101_irq_read(&irq) == ESP_OK && irq != 0) {
                handle_pmu_irq_flags(irq);
            }
        }

        /* --- wake on touch, or on a raised wrist ------------------------- */
        if (s_state == WATCH_POWER_ASLEEP) {
            /* The touch controller holds INT low while a finger is down
             * (the BSP configures the line active-low). */
            if (gpio_get_level(BSP_LCD_TOUCH_INT) == 0) {
                ESP_LOGD(TAG, "touch while asleep - waking");
                svc_power_notify_activity();
            } else if (svc_settings_get()->wake_on_raise &&
                       svc_settings_get()->imu_enable &&
                       qmi8658_is_present() && raise_detect(now)) {
                ESP_LOGD(TAG, "wrist raised - waking");
                svc_power_notify_activity();
            }
        }

        /* --- idle state machine ----------------------------------------- */
        const watch_settings_t *cfg = svc_settings_get();
        portENTER_CRITICAL(&s_activity_mux);
        const int64_t last = s_last_activity_us;
        portEXIT_CRITICAL(&s_activity_mux);
        const uint32_t idle_sec = (uint32_t)((now - last) / 1000000);

        if (cfg->idle_off_sec > 0 && idle_sec >= cfg->idle_off_sec) {
            enter_state(WATCH_POWER_ASLEEP);
        } else if (cfg->idle_dim_sec > 0 && idle_sec >= cfg->idle_dim_sec &&
                   s_state == WATCH_POWER_ACTIVE) {
            enter_state(WATCH_POWER_DIMMED);
        }

        vTaskDelay(pdMS_TO_TICKS((s_state == WATCH_POWER_ASLEEP) ? SLEEP_TICK_MS
                                                                 : IDLE_TICK_MS));
    }
}

/* ------------------------------------------------------------------ init */

static esp_err_t configure_pm(void)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 80,   /* the lowest the PSRAM timing tolerates */
        .light_sleep_enable = true,
    };
    ESP_RETURN_ON_ERROR(esp_pm_configure(&pm), TAG, "esp_pm_configure");

    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "watch_perf", &s_perf_lock),
                        TAG, "pm lock");

    /* Let the touch controller's interrupt pull the SoC out of light sleep,
     * so an idle watch is not woken by a timer just to find nothing to do. */
    gpio_wakeup_enable(BSP_LCD_TOUCH_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    ESP_LOGI(TAG, "DFS %d-%d MHz with automatic light sleep",
             pm.min_freq_mhz, pm.max_freq_mhz);
#else
    ESP_LOGW(TAG, "CONFIG_PM_ENABLE is off - idle current will be much higher");
#endif
    return ESP_OK;
}

esp_err_t svc_power_init(void)
{
    const esp_err_t pmu_err = axp2101_init(bsp_i2c_get_handle());
    if (pmu_err != ESP_OK) {
        ESP_LOGW(TAG, "no PMU: battery readings and power-off are unavailable");
    } else {
        (void)axp2101_read_status(&s_status);
    }

    ESP_RETURN_ON_ERROR(configure_pm(), TAG, "pm");

    s_last_activity_us = esp_timer_get_time();
    s_applied_brightness = 0xFF;   /* force the first apply to write through */
    apply_brightness(svc_settings_get()->brightness);

    if (xTaskCreatePinnedToCore(power_task, "watch_pwr", 4096, NULL, 4, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
