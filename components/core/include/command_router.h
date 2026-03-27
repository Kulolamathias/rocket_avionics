/**
 * @file components/core/include/command_router.h
 * @brief Command Router – controlled command dispatch with registration phase.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This module provides a strict bridge between the core (state_manager) and
 * services. It ensures that:
 *
 * - Each command has exactly one registered handler.
 * - No command execution occurs before all handlers are registered (router locked).
 * - Command execution is deterministic and synchronous.
 *
 * =============================================================================
 * LIFECYCLE MODEL
 * =============================================================================
 *
 * INIT PHASE:
 *   command_router_init()
 *
 * REGISTER PHASE:
 *   command_router_register_handler(...)   // called by services
 *
 * LOCK PHASE:
 *   command_router_lock()                  // called after all services registered
 *
 * RUN PHASE:
 *   command_router_execute(...)            // called by state_manager
 *
 * =============================================================================
 * SAFETY GUARANTEES
 * =============================================================================
 * - No duplicate handler registration.
 * - No registration after lock.
 * - No execution before lock.
 * - Bounds checking on command IDs.
 * - No dynamic memory allocation.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef COMMAND_ROUTER_H
#define COMMAND_ROUTER_H

#include "esp_err.h"
#include "command_types.h"
#include "command_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * HANDLER FUNCTION TYPE
 * ============================================================ */

/**
 * @brief Command handler function signature.
 *
 * @param context Service-specific context pointer (may be NULL)
 * @param params Command parameters (may be NULL, cast to correct struct type)
 * @return ESP_OK on success, error code otherwise
 *
 * @note Handlers must be:
 *       - Non-blocking or bounded execution
 *       - Deterministic
 *       - Not call command_router_execute recursively
 */
typedef esp_err_t (*command_handler_fn_t)(void *context, const command_param_union_t *params);

/* ============================================================
 * INIT PHASE
 * ============================================================ */

/**
 * @brief Initialize the command router.
 *
 * Clears all internal registrations and prepares the router for the
 * registration phase. Must be called once before any registration.
 */
void command_router_init(void);

/* ============================================================
 * REGISTER PHASE
 * ============================================================ */

/**
 * @brief Register a handler for a specific command.
 *
 * @param command Command ID
 * @param handler Handler function pointer
 * @param context Optional service context (may be NULL)
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if:
 *         - command is out of range
 *         - handler is NULL
 * @return ESP_ERR_INVALID_STATE if:
 *         - handler already registered for this command
 *         - router already locked
 *
 * @note Each command can be registered only once.
 */
esp_err_t command_router_register_handler(command_type_t command,
                                          command_handler_fn_t handler,
                                          void *context);

/* ============================================================
 * LOCK PHASE
 * ============================================================ */

/**
 * @brief Lock the command router after registration.
 *
 * Prevents any further handler registration. Must be called after all
 * services have registered their handlers.
 */
void command_router_lock(void);

/**
 * @brief Check if the command router is locked.
 *
 * @return true if router is locked (ready for runtime)
 * @return false if still in registration phase
 */
bool command_router_is_locked(void);

/* ============================================================
 * RUN PHASE
 * ============================================================ */

/**
 * @brief Execute a command.
 *
 * Routes the command to its registered handler. The router must be locked
 * before calling this function.
 *
 * @param command Command ID
 * @param params Command parameters (may be NULL)
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if command out of range
 * @return ESP_ERR_INVALID_STATE if router not locked
 * @return ESP_ERR_NOT_FOUND if no handler registered for command
 */
esp_err_t command_router_execute(command_type_t command,
                                 const command_param_union_t *params);

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_ROUTER_H */