/**
 * @file components/core/include/event_dispatcher.h
 * @brief Event Dispatcher – central event queue and routing.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Provides a deterministic event pipeline:
 *
 *   producers (services) → queue → state_manager
 *
 * The dispatcher owns the FreeRTOS queue and a dedicated task that forwards
 * events to state_manager. It ensures FIFO ordering and thread-safe posting.
 *
 * =============================================================================
 * LIFECYCLE MODEL
 * =============================================================================
 *
 * INIT PHASE:
 *   event_dispatcher_init()        // Creates queue
 *
 * START PHASE:
 *   event_dispatcher_start()       // Creates dispatcher task
 *
 * RUN PHASE:
 *   event_dispatcher_post()        // Called by services (task context)
 *   event_dispatcher_post_from_isr() // Called from ISR
 *
 * =============================================================================
 * DESIGN PROPERTIES
 * =============================================================================
 * - Thread-safe event posting (both task and ISR contexts)
 * - FIFO ordering (temporal determinism)
 * - Single consumer model (state_manager)
 * - No business logic – pure transport
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef EVENT_DISPATCHER_H
#define EVENT_DISPATCHER_H

#include "esp_err.h"
#include "event_types.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * INIT PHASE
 * ============================================================ */

/**
 * @brief Initialize the event dispatcher.
 *
 * Creates the internal FreeRTOS queue. Does NOT start the dispatcher task.
 * Must be called before any event posting.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_NO_MEM if queue creation fails
 * @return ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t event_dispatcher_init(void);

/* ============================================================
 * START PHASE
 * ============================================================ */

/**
 * @brief Start the dispatcher processing task.
 *
 * Creates a FreeRTOS task that dequeues events and forwards them to
 * state_manager_process_event(). Must be called after full system
 * initialization and before any events are posted.
 *
 * @return ESP_OK on success
 * @return ESP_FAIL if task creation fails
 * @return ESP_ERR_INVALID_STATE if:
 *         - dispatcher not initialized
 *         - dispatcher already started
 */
esp_err_t event_dispatcher_start(void);

/* ============================================================
 * EVENT POSTING (TASK CONTEXT)
 * ============================================================ */

/**
 * @brief Post an event from task context.
 *
 * Copies the event into the internal queue for asynchronous processing.
 * Non-blocking; returns immediately.
 *
 * @param event Pointer to event structure (must be valid, copied internally)
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if event is NULL
 * @return ESP_ERR_INVALID_STATE if dispatcher not started
 * @return ESP_ERR_TIMEOUT if queue is full (event dropped)
 */
esp_err_t event_dispatcher_post(const event_t *event);

/* ============================================================
 * EVENT POSTING (ISR CONTEXT)
 * ============================================================ */

/**
 * @brief Post an event from ISR context.
 *
 * Safe for use inside interrupt service routines. The event is copied into
 * the queue. If a higher-priority task is woken, *higher_priority_task_woken
 * is set to pdTRUE; the caller must then call portYIELD_FROM_ISR().
 *
 * @param event Pointer to event structure (must be valid, copied internally)
 * @param higher_priority_task_woken Pointer to a flag that will be set
 *        if a higher priority task was woken by this call.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if event is NULL
 * @return ESP_ERR_INVALID_STATE if dispatcher not started
 * @return ESP_FAIL if queue send fails (queue full)
 */
esp_err_t event_dispatcher_post_from_isr(const event_t *event,
                                         BaseType_t *higher_priority_task_woken);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_DISPATCHER_H */