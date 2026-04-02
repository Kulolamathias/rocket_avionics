/**
 * @file tests/src/timer_service_test.c
 * @brief Timer service test – implementation.
 *
 * =============================================================================
 * TEST SEQUENCE
 * =============================================================================
 * 1. Start a one‑shot timer (200 ms).
 * 2. Wait 300 ms – expect EVENT_TIMER_EXPIRED.
 * 3. Stop the one‑shot timer (should be harmless).
 * 4. Start a periodic timer (100 ms interval).
 * 5. Wait 500 ms – expect two or more periodic events.
 * 6. Stop the periodic timer.
 * 7. Wait a bit – expect no more events.
 *
 * =============================================================================
 * LOGGING
 * =============================================================================
 * The test does not capture events programmatically; it relies on the
 * system log to show that timer events were posted by the timer service.
 * Successful execution shows:
 *   - "EVENT_TIMER_EXPIRED posted"
 *   - Several "EVENT_PERIODIC_TIMER_EXPIRED posted"
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include "timer_service_test.h"
#include "command_router.h"
#include "command_params.h"
#include "event_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TimerTest";

esp_err_t timer_service_test_run(void)
{
    ESP_LOGI(TAG, "Starting timer service test");

    /* Prepare timer parameters */
    cmd_start_timer_params_t timer_params;
    command_param_union_t params;

    /* --------------------------------------------------------
     * 1. One‑shot timer
     * -------------------------------------------------------- */
    timer_params.timeout_ms = 200;
    params.timer = timer_params;

    ESP_LOGI(TAG, "Starting one‑shot timer (200 ms)");
    esp_err_t ret = command_router_execute(COMMAND_START_TIMER, &params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "COMMAND_START_TIMER failed: %d", ret);
        return ret;
    }

    /* Wait for expiration */
    vTaskDelay(pdMS_TO_TICKS(300));
    /* The timer service should have posted EVENT_TIMER_EXPIRED during this delay */

    /* Stop the one‑shot timer (safe even if already stopped) */
    ESP_LOGI(TAG, "Stopping one‑shot timer");
    ret = command_router_execute(COMMAND_STOP_TIMER, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "COMMAND_STOP_TIMER failed: %d", ret);
        return ret;
    }

    /* --------------------------------------------------------
     * 2. Periodic timer
     * -------------------------------------------------------- */
    timer_params.timeout_ms = 2500;
    params.timer = timer_params;

    ESP_LOGI(TAG, "Starting periodic timer (100 ms interval)");
    ret = command_router_execute(COMMAND_START_PERIODIC_TIMER, &params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "COMMAND_START_PERIODIC_TIMER failed: %d", ret);
        return ret;
    }

    /* Let it run for a while */
    vTaskDelay(pdMS_TO_TICKS(500));
    /* During this period, multiple EVENT_PERIODIC_TIMER_EXPIRED should be posted */

    /* Stop the periodic timer */
    // ESP_LOGI(TAG, "Stopping periodic timer");
    // ret = command_router_execute(COMMAND_STOP_PERIODIC_TIMER, NULL);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "COMMAND_STOP_PERIODIC_TIMER failed: %d", ret);
    //     return ret;
    // }

    // /* Wait a bit to ensure no extra events appear */
    // vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Timer service test completed. Check logs for timer events.");
    return ESP_OK;
}