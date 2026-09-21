#include "watch_svc/svc_camera.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_storage.h"
#include "watch_svc/svc_time.h"
#include "watch_hal/cam_mega.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "svc_camera";

#define PREVIEW_PIXELS  (SVC_CAMERA_PREVIEW_W * SVC_CAMERA_PREVIEW_H)
#define PREVIEW_BYTES   (PREVIEW_PIXELS * 2)
#define SPI_CHUNK        2048
#define STILL_CHUNK      4096

static svc_camera_status_t s_status;
static uint16_t           *s_frame;        /* RGB565, PSRAM */
static uint8_t            *s_luma;         /* grayscale copy for the detector */
static SemaphoreHandle_t   s_frame_mutex;
static volatile bool       s_preview_on;
static TaskHandle_t        s_task;

/* ------------------------------------------------------------------ helpers */

/* Build the grayscale plane the detector works on. Using the green channel
 * alone is a good enough stand-in for luma here and costs one shift instead
 * of three multiplies per pixel. */
static void extract_luma(const uint16_t *rgb, uint8_t *luma, size_t pixels)
{
    for (size_t i = 0; i < pixels; i++) {
        const uint16_t p = rgb[i];
        luma[i] = (uint8_t)(((p >> 5) & 0x3F) << 2);
    }
}

static esp_err_t grab_preview_frame(void)
{
    uint32_t len = 0;
    esp_err_t err = cam_mega_capture(CAM_MEGA_RES_QVGA, CAM_MEGA_FMT_RGB565, &len);
    if (err != ESP_OK) {
        return err;
    }

    /* Refuse a frame that is not the size we asked for rather than blit
     * whatever arrived into the canvas. */
    if (len < PREVIEW_BYTES) {
        ESP_LOGW(TAG, "short frame: %lu bytes, expected %d",
                 (unsigned long)len, PREVIEW_BYTES);
        cam_mega_flush();
        return ESP_ERR_INVALID_SIZE;
    }

    xSemaphoreTake(s_frame_mutex, portMAX_DELAY);

    uint8_t *dst = (uint8_t *)s_frame;
    uint32_t got = 0;
    while (got < PREVIEW_BYTES) {
        const uint32_t want = ((PREVIEW_BYTES - got) > SPI_CHUNK)
                              ? SPI_CHUNK : (PREVIEW_BYTES - got);
        const uint32_t n = cam_mega_read(dst + got, want);
        if (n == 0) {
            break;
        }
        got += n;
    }

    if (got == PREVIEW_BYTES) {
        /* The module sends RGB565 big-endian; LVGL wants it the other way
         * round on this target. */
        for (size_t i = 0; i < PREVIEW_PIXELS; i++) {
            s_frame[i] = (uint16_t)((s_frame[i] >> 8) | (s_frame[i] << 8));
        }
        extract_luma(s_frame, s_luma, PREVIEW_PIXELS);
    }

    xSemaphoreGive(s_frame_mutex);

    cam_mega_flush();   /* drop any tail the module still holds */

    if (got != PREVIEW_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static void preview_task(void *arg)
{
    (void)arg;
    int64_t window_start = 0;
    uint32_t window_frames = 0;

    for (;;) {
        if (!s_preview_on || !s_status.present) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (grab_preview_frame() == ESP_OK) {
            s_status.frames++;
            window_frames++;
            svc_event_post(WATCH_EV_CAMERA_FRAME, NULL, 0);
        } else {
            /* Back off rather than hammer a module that is misbehaving. */
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        /* Recompute the reported rate once a second. */
        const int64_t now = esp_timer_get_time();
        if (window_start == 0) {
            window_start = now;
        } else if (now - window_start >= 1000000) {
            s_status.fps = (uint8_t)window_frames;
            window_frames = 0;
            window_start = now;
        }

        taskYIELD();
    }
}

/* ---------------------------------------------------------------------- api */

esp_err_t svc_camera_init(void)
{
    s_frame_mutex = xSemaphoreCreateMutex();
    if (s_frame_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t err = cam_mega_init();
    if (err != ESP_OK) {
        s_status.present = false;
        ESP_LOGI(TAG, "no camera module attached");
        return err;
    }

    const cam_mega_info_t *info = cam_mega_get_info();
    s_status.present = true;
    s_status.width = SVC_CAMERA_PREVIEW_W;
    s_status.height = SVC_CAMERA_PREVIEW_H;
    if (info != NULL) {
        strncpy(s_status.model, info->model, sizeof(s_status.model) - 1);
        s_status.model[sizeof(s_status.model) - 1] = '\0';
    }

    /* Frame buffers go in PSRAM - 150 KB plus 75 KB would not fit in
     * internal RAM alongside LVGL and the network stacks. */
    s_frame = heap_caps_malloc(PREVIEW_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_luma  = heap_caps_malloc(PREVIEW_PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_frame == NULL || s_luma == NULL) {
        ESP_LOGE(TAG, "cannot allocate the preview buffers");
        free(s_frame); s_frame = NULL;
        free(s_luma);  s_luma = NULL;
        s_status.present = false;
        return ESP_ERR_NO_MEM;
    }
    memset(s_frame, 0, PREVIEW_BYTES);

    if (xTaskCreatePinnedToCore(preview_task, "watch_cam", 4096, NULL, 3, &s_task, 1)
        != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "camera ready (%s)", s_status.model);
    return ESP_OK;
}

bool svc_camera_available(void)
{
    return s_status.present;
}

const svc_camera_status_t *svc_camera_status(void)
{
    return &s_status;
}

esp_err_t svc_camera_preview(bool enable)
{
    if (!s_status.present) {
        return ESP_ERR_NOT_FOUND;
    }
    if (enable == s_preview_on) {
        return ESP_OK;
    }
    s_preview_on = enable;
    if (!enable) {
        s_status.fps = 0;
        /* Park the sensor while nothing is looking at it. */
        (void)cam_mega_set_low_power(true);
    } else {
        (void)cam_mega_set_low_power(false);
    }
    ESP_LOGI(TAG, "preview %s", enable ? "on" : "off");
    return ESP_OK;
}

const uint16_t *svc_camera_lock(uint32_t timeout_ms)
{
    if (s_frame == NULL || s_frame_mutex == NULL) {
        return NULL;
    }
    if (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return NULL;
    }
    if (s_status.frames == 0) {
        xSemaphoreGive(s_frame_mutex);
        return NULL;   /* nothing captured yet */
    }
    return s_frame;
}

void svc_camera_unlock(void)
{
    if (s_frame_mutex != NULL) {
        xSemaphoreGive(s_frame_mutex);
    }
}

/* Used by svc_vision; not part of the public header because the detector is
 * the only sensible consumer of a raw luma plane. */
const uint8_t *svc_camera_lock_luma(uint32_t timeout_ms)
{
    if (s_luma == NULL || s_frame_mutex == NULL) {
        return NULL;
    }
    if (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return NULL;
    }
    if (s_status.frames == 0) {
        xSemaphoreGive(s_frame_mutex);
        return NULL;
    }
    return s_luma;
}

static esp_err_t capture_still_locked(char *out_path, size_t path_len)
{
    uint32_t len = 0;
    if (cam_mega_capture(CAM_MEGA_RES_UXGA, CAM_MEGA_FMT_JPEG, &len) != ESP_OK || len == 0) {
        return ESP_FAIL;
    }

    char path[96];
    struct tm t;
    svc_time_now(&t);
    snprintf(path, sizeof(path), "%s/IMG_%02d%02d%02d_%02d%02d%02d.jpg",
             svc_storage_media_root(),
             (t.tm_year + 1900) % 100, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "cannot open %s", path);
        cam_mega_flush();
        return ESP_FAIL;
    }

    uint8_t *chunk = malloc(STILL_CHUNK);
    if (chunk == NULL) {
        fclose(f);
        cam_mega_flush();
        return ESP_ERR_NO_MEM;
    }

    uint32_t written = 0;
    for (;;) {
        const uint32_t n = cam_mega_read(chunk, STILL_CHUNK);
        if (n == 0) {
            break;
        }
        written += (uint32_t)fwrite(chunk, 1, n, f);
    }
    free(chunk);
    fclose(f);

    ESP_LOGI(TAG, "still saved: %s (%lu bytes)", path, (unsigned long)written);
    if (out_path != NULL && path_len > 0) {
        strncpy(out_path, path, path_len - 1);
        out_path[path_len - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t svc_camera_capture_still(char *out_path, size_t path_len)
{
    if (!s_status.present) {
        return ESP_ERR_NOT_FOUND;
    }

    /* Pause the preview so the two do not fight over the SPI bus. */
    const bool was_previewing = s_preview_on;
    s_preview_on = false;
    vTaskDelay(pdMS_TO_TICKS(50));

    const esp_err_t err = capture_still_locked(out_path, path_len);

    s_preview_on = was_previewing;
    return err;
}
