#include "watch_svc/svc_ota.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "svc_ota";

#define MANIFEST_MAX 512

static svc_ota_progress_t s_prog;
static char               s_image_url[256];
static volatile bool      s_abort;
static TaskHandle_t       s_task;
static volatile bool      s_want_check;
static volatile bool      s_want_install;

static void publish(void)
{
    svc_event_post(WATCH_EV_OTA_PROGRESS, &s_prog, sizeof(s_prog));
}

static void fail(const char *msg)
{
    s_prog.state = SVC_OTA_FAILED;
    strncpy(s_prog.error, (msg != NULL) ? msg : "", sizeof(s_prog.error) - 1);
    s_prog.error[sizeof(s_prog.error) - 1] = '\0';
    ESP_LOGE(TAG, "%s", s_prog.error);
    publish();
}

/* Compare dotted versions. Returns >0 when @p a is newer than @p b. */
static int version_cmp(const char *a, const char *b)
{
    for (int i = 0; i < 4; i++) {
        const long va = strtol(a, (char **)&a, 10);
        const long vb = strtol(b, (char **)&b, 10);
        if (va != vb) {
            return (va > vb) ? 1 : -1;
        }
        if (*a == '.') { a++; }
        if (*b == '.') { b++; }
        if (*a == '\0' && *b == '\0') {
            break;
        }
    }
    return 0;
}

/* Pull "key":"value" out of a flat JSON object. Enough for a two-field
 * manifest, and it keeps a JSON parser out of the dependency list. */
static bool json_string_field(const char *json, const char *key, char *out, size_t out_len)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }
    p = strchr(p + strlen(pattern), ':');
    if (p == NULL) {
        return false;
    }
    p = strchr(p, '"');
    if (p == NULL) {
        return false;
    }
    p++;
    const char *end = strchr(p, '"');
    if (end == NULL) {
        return false;
    }
    size_t n = (size_t)(end - p);
    if (n >= out_len) {
        n = out_len - 1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static void do_check(void)
{
    if (strlen(CONFIG_WATCH_OTA_MANIFEST_URL) == 0) {
        fail("No update server configured");
        return;
    }
    if (svc_wifi_status()->state != SVC_WIFI_CONNECTED) {
        fail("Wi-Fi is not connected");
        return;
    }

    s_prog.state = SVC_OTA_CHECKING;
    s_prog.percent = 0;
    s_prog.error[0] = '\0';
    publish();

    esp_http_client_config_t cfg = {
        .url = CONFIG_WATCH_OTA_MANIFEST_URL,
        .timeout_ms = 10000,
        .keep_alive_enable = false,
#if CONFIG_WATCH_OTA_SKIP_CERT_CHECK
        .skip_cert_common_name_check = true,
#else
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        fail("HTTP client failed");
        return;
    }

    char body[MANIFEST_MAX] = { 0 };
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        fail("Cannot reach the update server");
        return;
    }

    esp_http_client_fetch_headers(client);
    const int read = esp_http_client_read(client, body, sizeof(body) - 1);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200 || read <= 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Update server returned %d", status);
        fail(msg);
        return;
    }
    body[read] = '\0';

    char version[32] = { 0 };
    if (!json_string_field(body, "version", version, sizeof(version)) ||
        !json_string_field(body, "url", s_image_url, sizeof(s_image_url))) {
        fail("Manifest is not valid");
        return;
    }

    strncpy(s_prog.available_version, version, sizeof(s_prog.available_version) - 1);
    s_prog.available_version[sizeof(s_prog.available_version) - 1] = '\0';

    if (version_cmp(version, svc_ota_running_version()) > 0) {
        ESP_LOGI(TAG, "update available: %s (running %s)", version, svc_ota_running_version());
        s_prog.state = SVC_OTA_AVAILABLE;
    } else {
        ESP_LOGI(TAG, "already up to date (%s)", svc_ota_running_version());
        s_prog.state = SVC_OTA_UP_TO_DATE;
    }
    publish();
}

static void do_install(void)
{
    if (s_image_url[0] == '\0') {
        fail("No image to install");
        return;
    }

    s_prog.state = SVC_OTA_DOWNLOADING;
    s_prog.percent = 0;
    s_prog.downloaded = 0;
    s_prog.total = 0;
    publish();

    esp_http_client_config_t http_cfg = {
        .url = s_image_url,
        .timeout_ms = 20000,
        .keep_alive_enable = true,
#if CONFIG_WATCH_OTA_SKIP_CERT_CHECK
        .skip_cert_common_name_check = true,
#else
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    const esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK || handle == NULL) {
        fail("Download could not start");
        return;
    }

    s_prog.total = (uint32_t)esp_https_ota_get_image_size(handle);

    while (!s_abort) {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        s_prog.downloaded = (uint32_t)esp_https_ota_get_image_len_read(handle);
        if (s_prog.total > 0) {
            s_prog.percent = (uint8_t)((uint64_t)s_prog.downloaded * 100 / s_prog.total);
        }
        publish();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (s_abort) {
        esp_https_ota_abort(handle);
        s_prog.state = SVC_OTA_IDLE;
        s_prog.percent = 0;
        ESP_LOGW(TAG, "update cancelled");
        publish();
        return;
    }

    if (err != ESP_OK) {
        esp_https_ota_abort(handle);
        fail("Download failed");
        return;
    }

    if (!esp_https_ota_is_complete_data_received(handle)) {
        esp_https_ota_abort(handle);
        fail("Image is incomplete");
        return;
    }

    s_prog.state = SVC_OTA_INSTALLING;
    s_prog.percent = 100;
    publish();

    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        fail((err == ESP_ERR_OTA_VALIDATE_FAILED) ? "Image failed validation"
                                                  : "Install failed");
        return;
    }

    s_prog.state = SVC_OTA_DONE;
    ESP_LOGI(TAG, "update installed; reboot to run it");
    publish();
}

static void ota_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_want_check) {
            s_want_check = false;
            do_check();
        } else if (s_want_install) {
            s_want_install = false;
            s_abort = false;
            do_install();
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ---------------------------------------------------------------------- api */

esp_err_t svc_ota_init(void)
{
    /* If the running image is still pending verification, this boot getting
     * as far as here is the verification. Marking it valid cancels the
     * rollback that would otherwise happen on the next reset. */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "new image booted cleanly, marking it valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }

    s_prog.state = SVC_OTA_IDLE;

    if (xTaskCreatePinnedToCore(ota_task, "watch_ota", 8192, NULL, 4, &s_task, 0)
        != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

const svc_ota_progress_t *svc_ota_progress(void)
{
    return &s_prog;
}

const char *svc_ota_running_version(void)
{
    return CONFIG_WATCH_FW_VERSION;
}

esp_err_t svc_ota_check(void)
{
    if (s_prog.state == SVC_OTA_DOWNLOADING || s_prog.state == SVC_OTA_INSTALLING) {
        return ESP_ERR_INVALID_STATE;
    }
    s_want_check = true;
    return ESP_OK;
}

esp_err_t svc_ota_start(void)
{
    if (s_prog.state != SVC_OTA_AVAILABLE) {
        return ESP_ERR_INVALID_STATE;
    }
    s_want_install = true;
    return ESP_OK;
}

esp_err_t svc_ota_abort(void)
{
    s_abort = true;
    return ESP_OK;
}

esp_err_t svc_ota_reboot(void)
{
    if (s_prog.state != SVC_OTA_DONE) {
        return ESP_ERR_INVALID_STATE;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}
