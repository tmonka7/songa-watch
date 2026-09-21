/*
 * SD card and internal SPIFFS.
 *
 * The card is hot-pluggable, so mounting is retried and the UI is told
 * through WATCH_EV_SD_STATE rather than polling.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_STORAGE_MAX_ENTRIES 64
#define SVC_STORAGE_NAME_MAX    64

typedef struct {
    bool     mounted;
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t images_bytes;
    uint64_t videos_bytes;
    uint64_t other_bytes;
    char     card_name[24];
} svc_storage_info_t;

typedef struct {
    char     name[SVC_STORAGE_NAME_MAX];
    uint64_t size;
    bool     is_dir;
} svc_storage_entry_t;

/** @brief Mount SPIFFS and try the SD card. A missing card is not an error. */
esp_err_t svc_storage_init(void);

/** @brief Try to mount the card now. */
esp_err_t svc_storage_mount_sd(void);

/** @brief Unmount the card so it can be pulled safely. */
esp_err_t svc_storage_unmount_sd(void);

/** @brief Cached card usage. Refresh it with svc_storage_refresh(). */
const svc_storage_info_t *svc_storage_sd_info(void);

/**
 * @brief Re-walk the card and recompute the usage breakdown.
 *
 * Touches the whole directory tree, so it takes a moment on a full card.
 * Call it from a worker, not from an LVGL callback.
 */
esp_err_t svc_storage_refresh(void);

/** @brief List a directory. @return entries written. */
size_t svc_storage_list(const char *path, svc_storage_entry_t *out, size_t max);

/** @brief Free bytes on the internal SPIFFS partition. */
esp_err_t svc_storage_spiffs_usage(uint64_t *total, uint64_t *used);

/** @brief "1.2 GB", "845 MB". @p len >= 16. */
void svc_storage_format_size(uint64_t bytes, char *buf, size_t len);

/** @brief Where captures and recordings go - the card if present, else SPIFFS. */
const char *svc_storage_media_root(void);

#ifdef __cplusplus
}
#endif
