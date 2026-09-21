/*
 * On-device object detection.
 *
 * What ships here is a motion-and-blob detector: it runs frame differencing
 * on the camera's luma channel, groups the moving pixels into connected
 * regions, and reports those regions as objects with a confidence derived
 * from region density and stability. It runs in real time on the S3 and it
 * genuinely detects things - but it localises motion, it does not classify
 * what it sees, so every object comes back labelled SVC_VISION_CLASS_MOTION.
 *
 * A real classifier (a quantised YOLO through ESP-DL, for example) drops in
 * behind svc_vision_set_backend() without any screen changing: the Detection
 * screens already render whatever class names the backend reports. That
 * model is not bundled because it cannot be fetched during an offline build
 * and could not be verified on hardware here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_VISION_MAX_OBJECTS 8
#define SVC_VISION_LABEL_MAX   16

typedef enum {
    SVC_VISION_CLASS_MOTION = 0,
    SVC_VISION_CLASS_PERSON,
    SVC_VISION_CLASS_CAR,
    SVC_VISION_CLASS_OTHER,
} svc_vision_class_t;

typedef struct {
    svc_vision_class_t cls;
    char               label[SVC_VISION_LABEL_MAX];
    uint8_t            confidence;   /* 0-100 */
    /* Box in preview pixels, origin top-left. */
    int16_t            x, y, w, h;
} svc_vision_object_t;

typedef struct {
    uint8_t             count;
    svc_vision_object_t objects[SVC_VISION_MAX_OBJECTS];
    uint8_t             fps;
    uint32_t            infer_ms;    /* time the backend took on this frame */
    int64_t             timestamp_us;
} svc_vision_result_t;

/**
 * @brief A detector implementation.
 *
 * @param luma   Grayscale frame, @p w * @p h bytes.
 * @param out    Fill in objects[] and count. fps/infer_ms are set by the caller.
 */
typedef struct {
    const char *name;
    esp_err_t (*init)(uint16_t w, uint16_t h);
    esp_err_t (*detect)(const uint8_t *luma, uint16_t w, uint16_t h,
                        svc_vision_result_t *out);
    void      (*deinit)(void);
} svc_vision_backend_t;

/** @brief Start the detector service with the built-in motion backend. */
esp_err_t svc_vision_init(void);

/** @brief Swap in a different detector. Takes effect on the next start. */
esp_err_t svc_vision_set_backend(const svc_vision_backend_t *backend);

/** @brief The name of the active backend, for the About and Vision screens. */
const char *svc_vision_backend_name(void);

/**
 * @brief Start or stop detection.
 *
 * Needs the camera preview running. WATCH_EV_VISION_RESULT is posted for
 * each processed frame.
 */
esp_err_t svc_vision_set_running(bool running);

/** @brief Whether detection is running. */
bool svc_vision_is_running(void);

/** @brief The most recent result. */
const svc_vision_result_t *svc_vision_latest(void);

/** @brief Display name for a class, localised. */
const char *svc_vision_class_name(svc_vision_class_t cls);

#ifdef __cplusplus
}
#endif
