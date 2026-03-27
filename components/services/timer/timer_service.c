/**
 * @file components/services/timer/timer_service.c
 * @brief Timer Service implementation.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Implements the timer service using FreeRTOS software timers. It maintains
 * two timers: one-shot and periodic. Command handlers start/stop these timers,
 * and expiration callbacks post events to the core.
 *
 * =============================================================================
 * DESIGN NOTES
 * =============================================================================
 * - Uses FreeRTOS timers because they are lightweight and suitable for this use.
 * - Timers are created during init and never deleted.
 * - The one-shot timer is reused for each start; its period is changed on the fly.
 * - The periodic timer runs continuously when started; its period can be changed.
 * - Event posting uses service_post_event().
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include "timer_service.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "event_types.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "TIMER_SVC";

/* Static timer handles */
static TimerHandle_t s_oneshot_timer = NULL;
static TimerHandle_t s_periodic_timer = NULL;

/* ============================================================
 * TIMER CALLBACKS
 * ============================================================ */

static void oneshot_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    event_t ev = {
        .id = EVENT_TIMER_EXPIRED,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,  /* timer source */
        .data = { {0} }
    };
    service_post_event(&ev);
    ESP_LOGI(TAG, "EVENT_TIMER_EXPIRED posted");
}

static void periodic_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    event_t ev = {
        .id = EVENT_PERIODIC_TIMER_EXPIRED,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { {0} }
    };
    service_post_event(&ev);
    ESP_LOGI(TAG, "EVENT_PERIODIC_TIMER_EXPIRED posted");
}

/* ============================================================
 * COMMAND HANDLERS
 * ============================================================ */

static esp_err_t handle_start_timer(void *context, const command_param_union_t *params)
{
    (void)context;
    if (params == NULL) {
        ESP_LOGE(TAG, "START_TIMER called without parameters");
        return ESP_ERR_INVALID_ARG;
    }

    const cmd_start_timer_params_t *p = &params->timer;
    if (p->timeout_ms == 0) {
        ESP_LOGE(TAG, "Invalid timeout: 0");
        return ESP_ERR_INVALID_ARG;
    }

    /* Stop timer if running (safe even if not running) */
    xTimerStop(s_oneshot_timer, 0);

    /* Change period and start */
    if (xTimerChangePeriod(s_oneshot_timer, pdMS_TO_TICKS(p->timeout_ms), 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start one-shot timer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "One-shot timer started with %lu ms", p->timeout_ms);
    return ESP_OK;
}

static esp_err_t handle_stop_timer(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;
    if (xTimerStop(s_oneshot_timer, 0) != pdPASS) {
        /* Timer may not be running – that's fine */
        ESP_LOGI(TAG, "One-shot timer stop (maybe not running)");
    } else {
        ESP_LOGI(TAG, "One-shot timer stopped");
    }
    return ESP_OK;
}

static esp_err_t handle_start_periodic_timer(void *context, const command_param_union_t *params)
{
    (void)context;
    if (params == NULL) {
        ESP_LOGE(TAG, "START_PERIODIC_TIMER called without parameters");
        return ESP_ERR_INVALID_ARG;
    }

    const cmd_start_timer_params_t *p = &params->timer;
    if (p->timeout_ms == 0) {
        ESP_LOGE(TAG, "Invalid interval: 0");
        return ESP_ERR_INVALID_ARG;
    }

    /* Stop timer if running */
    xTimerStop(s_periodic_timer, 0);

    /* Change period and start */
    if (xTimerChangePeriod(s_periodic_timer, pdMS_TO_TICKS(p->timeout_ms), 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start periodic timer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Periodic timer started with %lu ms interval", p->timeout_ms);
    return ESP_OK;
}

static esp_err_t handle_stop_periodic_timer(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;
    if (xTimerStop(s_periodic_timer, 0) != pdPASS) {
        ESP_LOGI(TAG, "Periodic timer stop (maybe not running)");
    } else {
        ESP_LOGI(TAG, "Periodic timer stopped");
    }
    return ESP_OK;
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

esp_err_t timer_service_init(void)
{
    /* Create one-shot timer (initially stopped, dummy period) */
    s_oneshot_timer = xTimerCreate(
        "oneshot_tmr",
        pdMS_TO_TICKS(1000),  /* dummy period */
        pdFALSE,               /* one-shot */
        NULL,
        oneshot_timer_callback
    );
    if (s_oneshot_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create one-shot timer");
        return ESP_ERR_NO_MEM;
    }

    /* Create periodic timer (initially stopped) */
    s_periodic_timer = xTimerCreate(
        "periodic_tmr",
        pdMS_TO_TICKS(1000),  /* dummy period */
        pdTRUE,                /* auto-reload */
        NULL,
        periodic_timer_callback
    );
    if (s_periodic_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create periodic timer");
        xTimerDelete(s_oneshot_timer, 0);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Timer service initialized");
    return ESP_OK;
}

esp_err_t timer_service_register_handlers(void)
{
    esp_err_t ret;

    ret = service_register_command(COMMAND_START_TIMER, handle_start_timer, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(COMMAND_STOP_TIMER, handle_stop_timer, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(COMMAND_START_PERIODIC_TIMER, handle_start_periodic_timer, NULL);
    if (ret != ESP_OK) return ret;

    ret = service_register_command(COMMAND_STOP_PERIODIC_TIMER, handle_stop_periodic_timer, NULL);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Timer command handlers registered");
    return ESP_OK;
}

esp_err_t timer_service_start(void)
{
    ESP_LOGI(TAG, "Timer service started (timers idle)");
    return ESP_OK;
}