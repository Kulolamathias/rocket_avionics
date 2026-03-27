/**
 * @file components/services/common/service_interfaces.h
 * @brief Service-to-core interaction interface.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Provides a controlled interface for services to interact with the core layer.
 *
 * Services MUST NOT directly access:
 *   - event_dispatcher
 *   - command_router
 *
 * Instead, they MUST use the APIs defined in this file.
 *
 * =============================================================================
 * DESIGN PURPOSE
 * =============================================================================
 * - Decouple services from core implementation
 * - Provide stable and consistent APIs
 * - Enforce architectural boundaries
 *
 * =============================================================================
 * INTERACTION MODEL
 * =============================================================================
 * Services can:
 *
 *   1. Post events → service_post_event()
 *   2. Register command handlers → service_register_command()
 *
 * =============================================================================
 * LIFECYCLE CONSTRAINTS
 * =============================================================================
 * - service_register_command() → REGISTER phase only
 * - service_post_event()       → START/RUN phase only
 *
 * =============================================================================
 */

#ifndef SERVICE_INTERFACES_H
#define SERVICE_INTERFACES_H

#include "esp_err.h"
#include "event_types.h"
#include "command_types.h"
#include "command_params.h"
#include "command_router.h"
#include "event_dispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * COMMAND REGISTRATION (REGISTER PHASE)
 * ============================================================ */

/**
 * @brief Register a command handler for a service.
 *
 * Wraps command_router_register_handler().
 *
 * @param command Command ID to register.
 *
 * @param handler Function pointer to command handler.
 *                Must conform to command_handler_fn_t.
 *
 * @param context Optional service-specific context pointer.
 *                May be NULL if not required.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if inputs are invalid
 * @return ESP_ERR_INVALID_STATE if:
 *         - command already registered
 *         - router is locked (registration phase ended)
 *
 * @note Must be called during REGISTER phase only.
 */
static inline esp_err_t service_register_command(
    command_type_t command,
    command_handler_fn_t handler,
    void *context)
{
    return command_router_register_handler(command, handler, context);
}


/* ============================================================
 * EVENT POSTING (TASK CONTEXT)
 * ============================================================ */

/**
 * @brief Post an event to the system (task context).
 *
 * Wraps event_dispatcher_post().
 *
 * @param event Pointer to event structure.
 *              Must point to a valid event_t instance.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if event is NULL
 * @return ESP_ERR_INVALID_STATE if dispatcher not started
 * @return ESP_ERR_TIMEOUT if queue is full
 *
 * @note Intended for use in service tasks.
 */
static inline esp_err_t service_post_event(const event_t *event)
{
    return event_dispatcher_post(event);
}


/* ============================================================
 * EVENT POSTING (ISR CONTEXT)
 * ============================================================ */

/**
 * @brief Post an event from ISR context.
 *
 * Wraps event_dispatcher_post_from_isr().
 *
 * @param event Pointer to event structure.
 *
 * @param higher_priority_task_woken Pointer to a flag that will
 *              indicate if a higher-priority task was woken.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if inputs are invalid
 * @return ESP_FAIL if queue send fails
 *
 * @note Must be used only inside ISR context.
 */
static inline esp_err_t service_post_event_from_isr(
    const event_t *event,
    BaseType_t *higher_priority_task_woken)
{
    return event_dispatcher_post_from_isr(
        event,
        higher_priority_task_woken);
}

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_INTERFACES_H */