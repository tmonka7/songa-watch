/*
 * Songa Watch - ESP32-S3-Touch-AMOLED-2.06
 *
 * Boot order matters here:
 *
 *   1. settings   - everything else reads its configuration from these
 *   2. event bus  - services post to it from the moment they start
 *   3. I2C + BSP  - the display comes up early so the splash is quick
 *   4. UI         - shows the splash while the slow services finish
 *   5. services   - sensors, storage, audio, radios, camera, OTA
 *
 * The radios are the last thing started and only if the user left them on,
 * because a watch that switches its Wi-Fi on at every boot is a watch that
 * needs charging every day.
 */
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"

#include "watch_hal/watch_board.h"
#include "watch_svc/svc_audio.h"
#include "watch_svc/svc_ble.h"
#include "watch_svc/svc_camera.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_obd2.h"
#include "watch_svc/svc_ota.h"
#include "watch_svc/svc_power.h"
#include "watch_svc/svc_sensors.h"
#include "watch_svc/svc_settings.h"
#include "watch_svc/svc_storage.h"
#include "watch_svc/svc_time.h"
#include "watch_svc/svc_vision.h"
#include "watch_svc/svc_wifi.h"
#include "watch_ui/ui.h"

static const char *TAG = "main";

/* Reports the failure and carries on. A watch that boots to a screen saying
 * "no SD card" is far more useful than one that sits in a reboot loop
 * because an optional peripheral did not answer. */
static void try_init(const char *what, esp_err_t err)
{
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%-10s ok", what);
    } else {
        ESP_LOGW(TAG, "%-10s unavailable: %s", what, esp_err_to_name(err));
    }
}

/*
 * The LVGL port task configuration.
 *
 * Pinned to core 1 so rendering never contends with Wi-Fi and BLE, which
 * ESP-IDF keeps on core 0. Priority 4 sits above the service tasks and
 * below the radio stacks. 12 KB of stack: the deepest path is a flex
 * layout re-flow inside a scrolling container during a screen transition.
 */
static lvgl_port_cfg_t ui_port_cfg(void)
{
    lvgl_port_cfg_t cfg = ESP_LVGL_PORT_INIT_CONFIG();
    cfg.task_priority   = 4;
    cfg.task_stack      = 12288;
    cfg.task_affinity   = 1;
    cfg.task_max_sleep_ms = 500;
    cfg.timer_period_ms = 5;
    return cfg;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Songa Watch starting (%s)", CONFIG_WATCH_FW_VERSION);

    /* ---- 1. settings and the event bus ---- */
    ESP_ERROR_CHECK(svc_settings_init());
    ESP_ERROR_CHECK(svc_event_init());
    i18n_set_lang(svc_settings_get()->lang);

    /* ---- 2. board bring-up ---- */
    ESP_ERROR_CHECK(bsp_i2c_init());

    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ui_port_cfg(),
        .buffer_size   = BSP_LCD_H_RES * CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT,
        .double_buffer = false,
        .flags = {
            .buff_dma    = false,
            .buff_spiram = true,   /* 410x120x2 will not fit internally */
        },
    };
    if (bsp_display_start_with_config(&disp_cfg) == NULL) {
        ESP_LOGE(TAG, "display failed to start - nothing to show, halting");
        /* Without a panel there is no watch. Restarting gives the hardware
         * another chance rather than sitting dark forever. */
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }

    /* ---- 3. time, then the UI so the splash appears early ---- */
    try_init("rtc", svc_time_init());
    ESP_ERROR_CHECK(ui_start());

    /* ---- 4. power management ---- */
    try_init("power", svc_power_init());

    /* ---- 5. the rest, in rising order of cost ---- */
    try_init("sensors", svc_sensors_init());
    try_init("storage", svc_storage_init());
    try_init("audio",   svc_audio_init());
    try_init("ota",     svc_ota_init());

    /* The camera is an add-on; ESP_ERR_NOT_FOUND here is the normal case. */
    try_init("camera",  svc_camera_init());
    try_init("vision",  svc_vision_init());

    /* ---- 6. radios, only if they were left on ---- */
    try_init("wifi", svc_wifi_init());
    try_init("ble",  svc_ble_init());
    try_init("obd2", svc_obd2_init());

    const watch_settings_t *cfg = svc_settings_get();
    if (cfg->wifi_enable) {
        ESP_LOGI(TAG, "restoring Wi-Fi");
        svc_wifi_set_enabled(true);
        svc_wifi_connect_saved();
    }
    if (cfg->ble_enable) {
        ESP_LOGI(TAG, "restoring BLE");
        svc_ble_set_enabled(true);
    }

    ESP_LOGI(TAG, "up: %lu bytes free", (unsigned long)esp_get_free_heap_size());

    /* app_main returns and its task is reclaimed; everything from here runs
     * on the service tasks and the LVGL task. */
}
