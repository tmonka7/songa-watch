#include "watch_svc/svc_vision.h"
#include "watch_svc/svc_camera.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"

#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "svc_vision";

/* The detector works on a decimated copy of the preview. Quartering each
 * axis turns 320x240 into 80x60, which is 16x less work and still resolves
 * anything big enough to draw a box around. */
#define DEC          4
#define GRID_W       (SVC_CAMERA_PREVIEW_W / DEC)
#define GRID_H       (SVC_CAMERA_PREVIEW_H / DEC)
#define GRID_PIXELS  (GRID_W * GRID_H)

/* A cell counts as moving when it changes by more than this many levels. */
#define MOTION_THRESHOLD  18
/* Regions smaller than this are noise, not objects. */
#define MIN_REGION_CELLS  12

static const svc_vision_backend_t *s_backend;
static svc_vision_result_t         s_latest;
static volatile bool               s_running;
static TaskHandle_t                s_task;

/* ------------------------------------------------- built-in motion backend */

static uint8_t *s_prev;        /* previous decimated frame */
static uint8_t *s_cur;         /* this frame, decimated */
static uint8_t *s_mask;        /* per-cell: 0 still, 1 moving, >1 region id */
static int16_t *s_stack;       /* flood-fill worklist, avoids recursion */
static bool     s_have_prev;

static esp_err_t motion_init(uint16_t w, uint16_t h)
{
    (void)w; (void)h;
    /* All four live in PSRAM. The decimated frame alone is 4.8 KB, which
     * would take most of the detector task's stack if it were a local. */
    s_prev  = heap_caps_calloc(GRID_PIXELS, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_cur   = heap_caps_calloc(GRID_PIXELS, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_mask  = heap_caps_calloc(GRID_PIXELS, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_stack = heap_caps_calloc(GRID_PIXELS, sizeof(int16_t),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_prev == NULL || s_cur == NULL || s_mask == NULL || s_stack == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_have_prev = false;
    return ESP_OK;
}

static void motion_deinit(void)
{
    free(s_prev);  s_prev = NULL;
    free(s_cur);   s_cur = NULL;
    free(s_mask);  s_mask = NULL;
    free(s_stack); s_stack = NULL;
    s_have_prev = false;
}

/*
 * Flood-fill the moving cells into connected regions and report each one
 * whose bounding box is worth drawing.
 *
 * Iterative rather than recursive on purpose: a frame that is entirely in
 * motion would recurse GRID_PIXELS deep and blow the task stack.
 */
static esp_err_t motion_detect(const uint8_t *luma, uint16_t w, uint16_t h,
                               svc_vision_result_t *out)
{
    if (luma == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    out->count = 0;

    if (s_cur == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Decimate by averaging each DECxDEC block - plain sampling would make
     * the mask flicker on fine texture. */
    uint8_t *const cur = s_cur;
    for (int gy = 0; gy < GRID_H; gy++) {
        for (int gx = 0; gx < GRID_W; gx++) {
            uint32_t sum = 0;
            for (int dy = 0; dy < DEC; dy++) {
                const int sy = gy * DEC + dy;
                if (sy >= h) { continue; }
                for (int dx = 0; dx < DEC; dx++) {
                    const int sx = gx * DEC + dx;
                    if (sx >= w) { continue; }
                    sum += luma[sy * w + sx];
                }
            }
            cur[gy * GRID_W + gx] = (uint8_t)(sum / (DEC * DEC));
        }
    }

    if (!s_have_prev) {
        memcpy(s_prev, cur, GRID_PIXELS);
        s_have_prev = true;
        return ESP_OK;      /* nothing to compare against yet */
    }

    /* Difference mask. */
    uint32_t moving_cells = 0;
    for (int i = 0; i < GRID_PIXELS; i++) {
        const int diff = (int)cur[i] - (int)s_prev[i];
        const uint8_t mag = (uint8_t)((diff < 0) ? -diff : diff);
        s_mask[i] = (mag > MOTION_THRESHOLD) ? 1 : 0;
        moving_cells += s_mask[i];
    }

    /* A frame where almost everything moved is the camera panning, not an
     * object crossing it. Reporting one huge box would be worse than
     * reporting nothing. */
    if (moving_cells > (GRID_PIXELS * 3) / 4) {
        memcpy(s_prev, cur, GRID_PIXELS);
        return ESP_OK;
    }

    uint8_t next_id = 2;
    for (int seed = 0; seed < GRID_PIXELS && out->count < SVC_VISION_MAX_OBJECTS; seed++) {
        if (s_mask[seed] != 1) {
            continue;
        }

        const uint8_t id = next_id++;
        if (next_id == 0) { next_id = 2; }   /* wrap safely; ids are local */

        int sp = 0;
        s_stack[sp++] = (int16_t)seed;
        s_mask[seed] = id;

        int min_x = GRID_W, max_x = -1, min_y = GRID_H, max_y = -1;
        uint32_t cells = 0;
        uint32_t energy = 0;

        while (sp > 0) {
            const int idx = s_stack[--sp];
            const int x = idx % GRID_W;
            const int y = idx / GRID_W;

            cells++;
            energy += (uint32_t)abs((int)cur[idx] - (int)s_prev[idx]);
            if (x < min_x) { min_x = x; }
            if (x > max_x) { max_x = x; }
            if (y < min_y) { min_y = y; }
            if (y > max_y) { max_y = y; }

            /* 4-connected neighbours. */
            static const int dx[4] = { 1, -1, 0, 0 };
            static const int dy[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; k++) {
                const int nx = x + dx[k];
                const int ny = y + dy[k];
                if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) {
                    continue;
                }
                const int nidx = ny * GRID_W + nx;
                if (s_mask[nidx] == 1) {
                    s_mask[nidx] = id;
                    s_stack[sp++] = (int16_t)nidx;
                }
            }
        }

        if (cells < MIN_REGION_CELLS) {
            continue;
        }

        svc_vision_object_t *o = &out->objects[out->count++];
        o->cls = SVC_VISION_CLASS_MOTION;
        strncpy(o->label, svc_vision_class_name(SVC_VISION_CLASS_MOTION),
                SVC_VISION_LABEL_MAX - 1);
        o->label[SVC_VISION_LABEL_MAX - 1] = '\0';

        /* Confidence blends how densely the region fills its own bounding
         * box with how strongly it changed - a solid, high-contrast blob
         * scores well, a sparse shimmer does not. */
        const uint32_t box_cells = (uint32_t)(max_x - min_x + 1) * (uint32_t)(max_y - min_y + 1);
        const uint32_t fill = (box_cells > 0) ? (cells * 100 / box_cells) : 0;
        const uint32_t strength = (energy / cells) * 100 / 255;
        uint32_t conf = (fill * 2 + strength * 3) / 5;
        if (conf > 99) { conf = 99; }
        if (conf < 20) { conf = 20; }
        o->confidence = (uint8_t)conf;

        /* Back to preview pixels for the overlay. */
        o->x = (int16_t)(min_x * DEC);
        o->y = (int16_t)(min_y * DEC);
        o->w = (int16_t)((max_x - min_x + 1) * DEC);
        o->h = (int16_t)((max_y - min_y + 1) * DEC);
    }

    memcpy(s_prev, cur, GRID_PIXELS);
    return ESP_OK;
}

static const svc_vision_backend_t k_motion_backend = {
    .name   = "motion-blob",
    .init   = motion_init,
    .detect = motion_detect,
    .deinit = motion_deinit,
};

/* ---------------------------------------------------------------- service */

static void vision_task(void *arg)
{
    (void)arg;
    int64_t window_start = 0;
    uint32_t window_frames = 0;

    for (;;) {
        if (!s_running || s_backend == NULL || s_backend->detect == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        const uint8_t *luma = svc_camera_lock_luma(200);
        if (luma == NULL) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        svc_vision_result_t result = { 0 };
        const int64_t t0 = esp_timer_get_time();
        const esp_err_t err = s_backend->detect(luma, SVC_CAMERA_PREVIEW_W,
                                                SVC_CAMERA_PREVIEW_H, &result);
        const int64_t t1 = esp_timer_get_time();
        svc_camera_unlock();

        if (err == ESP_OK) {
            result.infer_ms = (uint32_t)((t1 - t0) / 1000);
            result.timestamp_us = t1;
            window_frames++;

            const int64_t now = t1;
            if (window_start == 0) {
                window_start = now;
            } else if (now - window_start >= 1000000) {
                s_latest.fps = (uint8_t)window_frames;
                window_frames = 0;
                window_start = now;
            }
            result.fps = s_latest.fps;

            s_latest = result;
            svc_event_post(WATCH_EV_VISION_RESULT, &s_latest, sizeof(s_latest));
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

esp_err_t svc_vision_init(void)
{
    s_backend = &k_motion_backend;
    if (xTaskCreatePinnedToCore(vision_task, "watch_vis", 6144, NULL, 3, &s_task, 1)
        != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "vision ready (backend: %s)", s_backend->name);
    return ESP_OK;
}

esp_err_t svc_vision_set_backend(const svc_vision_backend_t *backend)
{
    if (backend == NULL || backend->detect == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_running) {
        return ESP_ERR_INVALID_STATE;   /* stop it first */
    }
    if (s_backend != NULL && s_backend->deinit != NULL) {
        s_backend->deinit();
    }
    s_backend = backend;
    ESP_LOGI(TAG, "backend -> %s", backend->name);
    return ESP_OK;
}

const char *svc_vision_backend_name(void)
{
    return (s_backend != NULL) ? s_backend->name : "none";
}

esp_err_t svc_vision_set_running(bool running)
{
    if (running == s_running) {
        return ESP_OK;
    }
    if (running) {
        if (!svc_camera_available()) {
            return ESP_ERR_NOT_FOUND;
        }
        if (s_backend->init != NULL) {
            ESP_RETURN_ON_ERROR(s_backend->init(SVC_CAMERA_PREVIEW_W, SVC_CAMERA_PREVIEW_H),
                                TAG, "backend init");
        }
        memset(&s_latest, 0, sizeof(s_latest));
    } else {
        if (s_backend != NULL && s_backend->deinit != NULL) {
            s_backend->deinit();
        }
    }
    s_running = running;
    return ESP_OK;
}

bool svc_vision_is_running(void)
{
    return s_running;
}

const svc_vision_result_t *svc_vision_latest(void)
{
    return &s_latest;
}

const char *svc_vision_class_name(svc_vision_class_t cls)
{
    switch (cls) {
    case SVC_VISION_CLASS_PERSON: return "person";
    case SVC_VISION_CLASS_CAR:    return "car";
    case SVC_VISION_CLASS_MOTION: return i18n(STR_OBJECT);
    default:                      return "object";
    }
}
