/**
 * @file components/core/include/state_manager.h
 * @brief State Manager – deterministic transition engine.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This module is the sole authority for system behavior. It:
 *
 * - Consumes events from event_dispatcher
 * - Updates system context based on event data
 * - Evaluates the transition table (state + event + condition)
 * - Executes command batches (via command_router)
 *
 * =============================================================================
 * DESIGN MODEL
 * =============================================================================
 *
 *   STATE + EVENT + CONDITION → NEXT_STATE + COMMANDS
 *
 * - Transitions are deterministic (first match wins)
 * - Conditions are pure functions (read context only)
 * - Commands may include parameter preparers
 *
 * =============================================================================
 * DETERMINISM GUARANTEES
 * =============================================================================
 * - The same event, in the same state, with the same context,
 *   ALWAYS produces the same transition and command batch.
 * - Transition rules are evaluated in fixed order (table order).
 * - No polling – system reacts only to events.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include "esp_err.h"
#include "event_types.h"
#include "system_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * INITIALIZATION
 * ============================================================ */

/**
 * @brief Initialize the state manager.
 *
 * Sets up the internal system context with the provided initial values.
 * Must be called once during system startup, after command_router_init()
 * and event_dispatcher_init(), but before event_dispatcher_start().
 *
 * @param initial_context Pointer to initial system context values.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if initial_context is NULL.
 */
esp_err_t state_manager_init(const system_context_t *initial_context);

/* ============================================================
 * EVENT PROCESSING
 * ============================================================ */

/**
 * @brief Process an incoming event.
 *
 * This function is called by the event_dispatcher task. It:
 * 1. Updates the system context with data from the event.
 * 2. Evaluates the transition table in declared order.
 * 3. If a rule matches, executes the command batch and updates the system state.
 *
 * @param event Pointer to event structure (must be valid).
 * @return ESP_OK always (errors are logged internally).
 */
esp_err_t state_manager_process_event(const event_t *event);

/* ============================================================
 * CONTEXT ACCESS
 * ============================================================ */

/**
 * @brief Get read-only pointer to the current system context.
 *
 * This function is provided for debugging and for conditions that
 * need to read the context. The context is owned by state_manager
 * and must not be modified by callers.
 *
 * @return const pointer to system_context_t.
 */
const system_context_t* state_manager_get_context(void);

#ifdef __cplusplus
}
#endif

#endif /* STATE_MANAGER_H */