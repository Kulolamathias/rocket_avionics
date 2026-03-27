/**
 * @file components/core/src/event_dispatcher.c
 * @brief Event Dispatcher – implementation.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Implements the FreeRTOS queue and dedicated dispatcher task.
 * No event filtering, prioritization, or business logic.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - Dispatcher task runs at fixed priority (configurable, default = 5).
 * - Queue length is fixed at compile time (EVENT_QUEUE_LENGTH).
 * - Event structure is copied by value – no pointers are stored.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include "event_dispatcher.h"
#include "state_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdbool.h>

/* ============================================================
 * CONFIGURATION
 * ============================================================ */

#define EVENT_QUEUE_LENGTH     32
#define DISPATCHER_STACK_SIZE  4096
#define DISPATCHER_PRIORITY    5

/* ============================================================
 * INTERNAL STATE
 * ============================================================ */

static QueueHandle_t s_event_queue = NULL;
static TaskHandle_t s_dispatcher_task = NULL;
static bool s_started = false;

static const char *TAG = "event_dispatcher";

/* ============================================================
 * DISPATCHER TASK
 * ============================================================ */

static void event_dispatcher_task(void *pvParameters)
{
    (void)pvParameters;
    event_t event;

    ESP_LOGI(TAG, "Event dispatcher task started");

    while (1) {
        if (xQueueReceive(s_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG, "Dispatching event: %d", event.id);
            esp_err_t ret = state_manager_process_event(&event);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Event %d processing returned %d", event.id, ret);
            }
        }
    }
}

/* ============================================================
 * INIT PHASE
 * ============================================================ */

esp_err_t event_dispatcher_init(void)
{
    if (s_event_queue != NULL) {
        ESP_LOGE(TAG, "Event dispatcher already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_event_queue = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(event_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    s_started = false;
    s_dispatcher_task = NULL;

    ESP_LOGI(TAG, "Event dispatcher initialized (queue size: %d)", EVENT_QUEUE_LENGTH);
    return ESP_OK;
}

/* ============================================================
 * START PHASE
 * ============================================================ */

esp_err_t event_dispatcher_start(void)
{
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "Event dispatcher not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_started) {
        ESP_LOGW(TAG, "Event dispatcher already started");
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ret = xTaskCreate(
        event_dispatcher_task,
        "event_dispatcher",
        DISPATCHER_STACK_SIZE,
        NULL,
        DISPATCHER_PRIORITY,
        &s_dispatcher_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create dispatcher task");
        return ESP_FAIL;
    }

    s_started = true;
    ESP_LOGI(TAG, "Event dispatcher started");
    return ESP_OK;
}

/* ============================================================
 * EVENT POSTING (TASK CONTEXT)
 * ============================================================ */

esp_err_t event_dispatcher_post(const event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xQueueSend(s_event_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, event %d dropped", event->id);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/* ============================================================
 * EVENT POSTING (ISR CONTEXT)
 * ============================================================ */

esp_err_t event_dispatcher_post_from_isr(const event_t *event,
                                         BaseType_t *higher_priority_task_woken)
{
    if (event == NULL || higher_priority_task_woken == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ret = xQueueSendFromISR(s_event_queue, event, higher_priority_task_woken);
    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full from ISR, event %d dropped", event->id);
        return ESP_FAIL;
    }

    return ESP_OK;
}