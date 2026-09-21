#include "watch_svc/svc_event.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "svc_event";

ESP_EVENT_DEFINE_BASE(WATCH_EVENT);

/* Deep enough to absorb a burst from several services at once without
 * making the UI's slowest handler block a driver task. */
#define EVENT_QUEUE_SIZE   24
#define EVENT_TASK_STACK   4096
#define EVENT_TASK_PRIO    5

static esp_event_loop_handle_t s_loop;

esp_err_t svc_event_init(void)
{
    if (s_loop != NULL) {
        return ESP_OK;
    }
    const esp_event_loop_args_t args = {
        .queue_size      = EVENT_QUEUE_SIZE,
        .task_name       = "watch_ev",
        .task_priority   = EVENT_TASK_PRIO,
        .task_stack_size = EVENT_TASK_STACK,
        .task_core_id    = 0,
    };
    ESP_RETURN_ON_ERROR(esp_event_loop_create(&args, &s_loop), TAG, "loop create");
    ESP_LOGI(TAG, "event loop up");
    return ESP_OK;
}

esp_event_loop_handle_t svc_event_loop(void)
{
    return s_loop;
}

esp_err_t svc_event_post(watch_event_id_t id, const void *data, size_t len)
{
    if (s_loop == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Zero ticks: a status update is worth less than the driver task that
     * would have to wait for the queue to drain. */
    const esp_err_t err = esp_event_post_to(s_loop, WATCH_EVENT, (int32_t)id, data, len, 0);
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGD(TAG, "queue full, dropped event %d", (int)id);
    }
    return err;
}

esp_err_t svc_event_subscribe(int32_t id, esp_event_handler_t handler, void *arg)
{
    if (s_loop == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_handler_register_with(s_loop, WATCH_EVENT, id, handler, arg);
}

esp_err_t svc_event_unsubscribe(int32_t id, esp_event_handler_t handler)
{
    if (s_loop == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_handler_unregister_with(s_loop, WATCH_EVENT, id, handler);
}
