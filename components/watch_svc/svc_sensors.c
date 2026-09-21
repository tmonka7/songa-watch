#include "watch_svc/svc_sensors.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_settings.h"
#include "watch_hal/qmi8658.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "bsp/esp-bsp.h"

static const char *TAG = "svc_sensors";

/* 4 Hz to the UI. The IMU itself runs at 125 Hz; the graph does not need
 * more than this and every extra wake costs battery. */
#define SAMPLE_PERIOD_MS   250
#define SLEEP_PERIOD_MS   5000

typedef struct {
    float ax[SVC_SENSORS_HISTORY_LEN];
    float ay[SVC_SENSORS_HISTORY_LEN];
    float az[SVC_SENSORS_HISTORY_LEN];
    float gx[SVC_SENSORS_HISTORY_LEN];
    float gy[SVC_SENSORS_HISTORY_LEN];
    float gz[SVC_SENSORS_HISTORY_LEN];
    size_t head;     /* next write index */
    size_t filled;
} history_t;

static svc_sensors_sample_t s_latest;
static history_t            s_hist;
static SemaphoreHandle_t    s_mutex;
static bool                 s_available;
static bool                 s_low_power;
static uint32_t             s_step_base;     /* hardware count at last reset */

static void history_push(const svc_sensors_sample_t *s)
{
    const size_t i = s_hist.head;
    s_hist.ax[i] = s->ax;
    s_hist.ay[i] = s->ay;
    s_hist.az[i] = s->az;
    s_hist.gx[i] = s->gx;
    s_hist.gy[i] = s->gy;
    s_hist.gz[i] = s->gz;
    s_hist.head = (i + 1) % SVC_SENSORS_HISTORY_LEN;
    if (s_hist.filled < SVC_SENSORS_HISTORY_LEN) {
        s_hist.filled++;
    }
}

static void sensors_task(void *arg)
{
    (void)arg;
    uint32_t last_steps = UINT32_MAX;

    for (;;) {
        const watch_settings_t *cfg = svc_settings_get();
        const uint32_t period = s_low_power ? SLEEP_PERIOD_MS : SAMPLE_PERIOD_MS;

        if (cfg->imu_enable && s_available) {
            qmi8658_data_t d;
            uint32_t hw_steps = 0;

            /* In low power the gyro is off, so its readings are stale; zero
             * them rather than showing the last values from before sleep. */
            if (qmi8658_read(&d) == ESP_OK) {
                if (s_low_power) {
                    d.gx = d.gy = d.gz = 0.0f;
                }
                (void)qmi8658_read_steps(&hw_steps);

                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_latest.ax = d.ax;
                s_latest.ay = d.ay;
                s_latest.az = d.az;
                s_latest.gx = d.gx;
                s_latest.gy = d.gy;
                s_latest.gz = d.gz;
                s_latest.temp_c = d.temp_c;
                s_latest.steps = (hw_steps >= s_step_base) ? (hw_steps - s_step_base) : hw_steps;
                s_latest.timestamp_us = esp_timer_get_time();
                if (!s_low_power) {
                    history_push(&s_latest);
                }
                const svc_sensors_sample_t snapshot = s_latest;
                xSemaphoreGive(s_mutex);

                if (!s_low_power) {
                    svc_event_post(WATCH_EV_SENSOR_SAMPLE, &snapshot, sizeof(snapshot));
                }
                if (snapshot.steps != last_steps) {
                    last_steps = snapshot.steps;
                    svc_event_post(WATCH_EV_STEPS_CHANGED, &last_steps, sizeof(last_steps));
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(period));
    }
}

esp_err_t svc_sensors_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const qmi8658_config_t cfg = QMI8658_DEFAULT_CONFIG();
    if (qmi8658_init(bsp_i2c_get_handle(), &cfg) != ESP_OK) {
        ESP_LOGW(TAG, "no IMU: motion screens will say so");
        s_available = false;
    } else {
        s_available = true;
        if (qmi8658_pedometer_enable(true) == ESP_OK) {
            /* Take the current hardware total as zero rather than resetting
             * it, so steps taken before this boot are not double counted. */
            (void)qmi8658_read_steps(&s_step_base);
            ESP_LOGI(TAG, "hardware pedometer on (base %lu)", (unsigned long)s_step_base);
        }
    }

    if (xTaskCreatePinnedToCore(sensors_task, "watch_imu", 4096, NULL, 3, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool svc_sensors_available(void)
{
    return s_available;
}

const svc_sensors_sample_t *svc_sensors_latest(void)
{
    return &s_latest;
}

uint32_t svc_sensors_steps(void)
{
    return s_latest.steps;
}

esp_err_t svc_sensors_reset_steps(void)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = qmi8658_reset_steps();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_step_base = 0;
    s_latest.steps = 0;
    xSemaphoreGive(s_mutex);
    const uint32_t zero = 0;
    svc_event_post(WATCH_EV_STEPS_CHANGED, &zero, sizeof(zero));
    return err;
}

size_t svc_sensors_get_history(float *ax, float *ay, float *az,
                               float *gx, float *gy, float *gz)
{
    if (s_mutex == NULL) {
        return 0;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    const size_t n = s_hist.filled;
    /* Unwrap the ring so callers get oldest-first without knowing about it. */
    const size_t start = (s_hist.filled == SVC_SENSORS_HISTORY_LEN) ? s_hist.head : 0;
    for (size_t i = 0; i < n; i++) {
        const size_t src = (start + i) % SVC_SENSORS_HISTORY_LEN;
        if (ax) { ax[i] = s_hist.ax[src]; }
        if (ay) { ay[i] = s_hist.ay[src]; }
        if (az) { az[i] = s_hist.az[src]; }
        if (gx) { gx[i] = s_hist.gx[src]; }
        if (gy) { gy[i] = s_hist.gy[src]; }
        if (gz) { gz[i] = s_hist.gz[src]; }
    }

    xSemaphoreGive(s_mutex);
    return n;
}

esp_err_t svc_sensors_set_low_power(bool low_power)
{
    if (!s_available || low_power == s_low_power) {
        return ESP_OK;
    }
    s_low_power = low_power;

    if (low_power) {
        /* Gyro off, accelerometer only. The pedometer runs off the
         * accelerometer, so steps keep counting while the watch sleeps.
         *
         * 3 Hz is the cheapest setting, but a raise gesture takes well
         * under a second: at 3 Hz the watch would see two or three samples
         * of it and light the screen after the wrist had already stopped.
         * 21 Hz is still an accel-only low-power mode costing tens of
         * microamps, and it is what makes the gesture detectable at all. */
        const bool raise = svc_settings_get()->wake_on_raise;
        return qmi8658_enter_low_power(raise ? QMI8658_ODR_LOWPOWER_21HZ
                                             : QMI8658_ODR_LOWPOWER_3HZ);
    }
    return qmi8658_exit_low_power();
}
