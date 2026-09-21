#include "watch_svc/svc_storage.h"
#include "watch_svc/svc_event.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"

#include "bsp/esp-bsp.h"

static const char *TAG = "svc_storage";

/* How deep svc_storage_refresh() walks. Media lands one or two levels down;
 * anything deeper is counted as "other" without descending, so a card full
 * of nested folders cannot stall the scan. */
#define WALK_MAX_DEPTH 3

static svc_storage_info_t s_info;

static bool has_ext(const char *name, const char *const *exts, size_t n)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (strcasecmp(dot, exts[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void walk(const char *path, int depth)
{
    static const char *const image_ext[] = { ".jpg", ".jpeg", ".png", ".bmp", ".gif" };
    static const char *const video_ext[] = { ".mp4", ".avi", ".mov", ".mjpeg", ".mkv" };

    DIR *dir = opendir(path);
    if (dir == NULL) {
        return;
    }

    struct dirent *ent;
    char child[256];
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        const int n = snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(child)) {
            continue;   /* path too long to follow */
        }

        struct stat st;
        if (stat(child, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (depth + 1 < WALK_MAX_DEPTH) {
                walk(child, depth + 1);
            }
            continue;
        }

        const uint64_t size = (uint64_t)st.st_size;
        if (has_ext(ent->d_name, image_ext, sizeof(image_ext) / sizeof(image_ext[0]))) {
            s_info.images_bytes += size;
        } else if (has_ext(ent->d_name, video_ext, sizeof(video_ext) / sizeof(video_ext[0]))) {
            s_info.videos_bytes += size;
        } else {
            s_info.other_bytes += size;
        }
    }
    closedir(dir);
}

esp_err_t svc_storage_init(void)
{
    esp_err_t err = bsp_spiffs_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    }

    /* A missing card is the normal case, not a failure. */
    if (svc_storage_mount_sd() != ESP_OK) {
        ESP_LOGI(TAG, "no SD card at boot");
    }
    return ESP_OK;
}

esp_err_t svc_storage_mount_sd(void)
{
    if (s_info.mounted) {
        return ESP_OK;
    }

    const esp_err_t err = bsp_sdcard_mount();
    if (err != ESP_OK) {
        s_info.mounted = false;
        return err;
    }

    s_info.mounted = true;
    if (bsp_sdcard != NULL) {
        strncpy(s_info.card_name, bsp_sdcard->cid.name, sizeof(s_info.card_name) - 1);
        s_info.card_name[sizeof(s_info.card_name) - 1] = '\0';
    }
    ESP_LOGI(TAG, "SD card mounted (%s)", s_info.card_name);

    (void)svc_storage_refresh();
    const bool mounted = true;
    svc_event_post(WATCH_EV_SD_STATE, &mounted, sizeof(mounted));
    return ESP_OK;
}

esp_err_t svc_storage_unmount_sd(void)
{
    if (!s_info.mounted) {
        return ESP_OK;
    }
    const esp_err_t err = bsp_sdcard_unmount();
    memset(&s_info, 0, sizeof(s_info));
    const bool mounted = false;
    svc_event_post(WATCH_EV_SD_STATE, &mounted, sizeof(mounted));
    ESP_LOGI(TAG, "SD card unmounted");
    return err;
}

const svc_storage_info_t *svc_storage_sd_info(void)
{
    return &s_info;
}

esp_err_t svc_storage_refresh(void)
{
    if (!s_info.mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    /* esp_vfs_fat_info() works off the mount point and handles the cluster
     * and sector arithmetic, which beats reaching into the FATFS internals
     * and guessing at the sector size. */
    uint64_t total = 0, freeb = 0;
    if (esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total, &freeb) == ESP_OK) {
        s_info.total_bytes = total;
        s_info.free_bytes  = freeb;
        s_info.used_bytes  = (total > freeb) ? (total - freeb) : 0;
    }

    s_info.images_bytes = 0;
    s_info.videos_bytes = 0;
    s_info.other_bytes = 0;
    walk(BSP_SD_MOUNT_POINT, 0);

    return ESP_OK;
}

size_t svc_storage_list(const char *path, svc_storage_entry_t *out, size_t max)
{
    if (path == NULL || out == NULL || max == 0) {
        return 0;
    }
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return 0;
    }

    size_t n = 0;
    struct dirent *ent;
    char child[256];
    while (n < max && (ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        strncpy(out[n].name, ent->d_name, SVC_STORAGE_NAME_MAX - 1);
        out[n].name[SVC_STORAGE_NAME_MAX - 1] = '\0';

        const int len = snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        struct stat st;
        if (len > 0 && (size_t)len < sizeof(child) && stat(child, &st) == 0) {
            out[n].size = (uint64_t)st.st_size;
            out[n].is_dir = S_ISDIR(st.st_mode);
        } else {
            out[n].size = 0;
            out[n].is_dir = false;
        }
        n++;
    }
    closedir(dir);
    return n;
}

esp_err_t svc_storage_spiffs_usage(uint64_t *total, uint64_t *used)
{
    size_t t = 0, u = 0;
    ESP_RETURN_ON_ERROR(esp_spiffs_info(CONFIG_BSP_SPIFFS_PARTITION_LABEL, &t, &u),
                        TAG, "spiffs info");
    if (total != NULL) { *total = t; }
    if (used  != NULL) { *used  = u; }
    return ESP_OK;
}

void svc_storage_format_size(uint64_t bytes, char *buf, size_t len)
{
    if (buf == NULL || len < 8) {
        return;
    }
    if (bytes >= (1ULL << 30)) {
        snprintf(buf, len, "%.1f GB", (double)bytes / (double)(1ULL << 30));
    } else if (bytes >= (1ULL << 20)) {
        snprintf(buf, len, "%.1f MB", (double)bytes / (double)(1ULL << 20));
    } else if (bytes >= (1ULL << 10)) {
        snprintf(buf, len, "%.0f KB", (double)bytes / (double)(1ULL << 10));
    } else {
        snprintf(buf, len, "%llu B", (unsigned long long)bytes);
    }
}

const char *svc_storage_media_root(void)
{
    /* Prefer the card; fall back to the internal partition so captures and
     * recordings always have somewhere to land. */
    return s_info.mounted ? BSP_SD_MOUNT_POINT : BSP_SPIFFS_MOUNT_POINT;
}
