/*
 * Over-the-air firmware update.
 *
 * Two app slots in the partition table, HTTPS transport against the
 * ESP-IDF certificate bundle, and the new image is left pending until it
 * boots successfully - a bad build rolls back on the next reset instead of
 * bricking the watch.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SVC_OTA_IDLE = 0,
    SVC_OTA_CHECKING,
    SVC_OTA_AVAILABLE,
    SVC_OTA_UP_TO_DATE,
    SVC_OTA_DOWNLOADING,
    SVC_OTA_INSTALLING,
    SVC_OTA_DONE,
    SVC_OTA_FAILED,
} svc_ota_state_t;

typedef struct {
    svc_ota_state_t state;
    uint8_t         percent;
    uint32_t        downloaded;
    uint32_t        total;
    char            available_version[32];
    char            error[64];
} svc_ota_progress_t;

/** @brief Mark the running image valid, cancelling any pending rollback. */
esp_err_t svc_ota_init(void);

/** @brief Current progress. */
const svc_ota_progress_t *svc_ota_progress(void);

/** @brief The version string baked into the running app. */
const char *svc_ota_running_version(void);

/**
 * @brief Ask the update server what is available.
 *
 * Needs Wi-Fi. Asynchronous; watch WATCH_EV_OTA_PROGRESS.
 */
esp_err_t svc_ota_check(void);

/** @brief Download and install whatever svc_ota_check() found. */
esp_err_t svc_ota_start(void);

/** @brief Abort an in-flight download. */
esp_err_t svc_ota_abort(void);

/** @brief Reboot into the freshly written image. */
esp_err_t svc_ota_reboot(void);

#ifdef __cplusplus
}
#endif
