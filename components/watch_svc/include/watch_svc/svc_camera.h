/*
 * Camera preview and stills.
 *
 * Backed by the Arducam Mega SPI module (components/watch_hal/cam_mega.c).
 * The board has no camera of its own and no free DVP bus, so if no module
 * is attached every entry point here reports ESP_ERR_NOT_FOUND and the
 * camera screens say so rather than showing a frozen frame.
 *
 * Preview frames are pulled from the module as RGB565, not JPEG. The Mega
 * can emit either, and taking RGB565 means the watch never needs a JPEG
 * decoder in the preview path - which keeps a decoder library out of the
 * dependency set entirely and removes the decode from the frame budget.
 * The cost is bandwidth: 320x240x2 bytes over SPI at 8 MHz is about 6 fps.
 * Stills still use JPEG, but only to write straight to a file, so those
 * bytes are never decoded either.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Preview size. 320x240 is what the S3 can decode and blit fast enough to
 * feel live; the stills path uses the module's full resolution. */
#define SVC_CAMERA_PREVIEW_W 320
#define SVC_CAMERA_PREVIEW_H 240

typedef struct {
    bool     present;
    char     model[8];
    uint16_t width;
    uint16_t height;
    uint8_t  fps;            /* measured, not requested */
    uint32_t frames;
} svc_camera_status_t;

/** @brief Probe for the module. ESP_ERR_NOT_FOUND when nothing is attached. */
esp_err_t svc_camera_init(void);

/** @brief Whether a module answered at init. */
bool svc_camera_available(void);

/** @brief Live status, including the measured frame rate. */
const svc_camera_status_t *svc_camera_status(void);

/**
 * @brief Start or stop the preview task.
 *
 * While running, WATCH_EV_CAMERA_FRAME is posted for each decoded frame.
 * Stop it when leaving the camera screens: the SPI reads and the JPEG
 * decode together are the heaviest thing the watch does.
 */
esp_err_t svc_camera_preview(bool enable);

/**
 * @brief Borrow the decoded RGB565 preview buffer.
 *
 * Held until svc_camera_unlock(). Returns NULL when no frame has arrived.
 * Dimensions are SVC_CAMERA_PREVIEW_W x SVC_CAMERA_PREVIEW_H.
 */
const uint16_t *svc_camera_lock(uint32_t timeout_ms);
void svc_camera_unlock(void);

/**
 * @brief Borrow the grayscale copy of the same frame.
 *
 * Shares the lock with svc_camera_lock(), so release it with
 * svc_camera_unlock() too, and do not hold both at once. This exists for
 * svc_vision - detectors want luma, and deriving it once while the frame is
 * being unpacked is cheaper than every backend doing it again.
 */
const uint8_t *svc_camera_lock_luma(uint32_t timeout_ms);

/**
 * @brief Take a full-resolution JPEG and write it under the media root.
 *
 * @param[out] out_path Optional; receives the file written.
 */
esp_err_t svc_camera_capture_still(char *out_path, size_t path_len);

#ifdef __cplusplus
}
#endif
