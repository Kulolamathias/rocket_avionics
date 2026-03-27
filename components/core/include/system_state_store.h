/**
 * @file components/core/include/system_state_store.h
 * @brief Authoritative storage and control of system state.
 *
 * This module owns the current system state.
 * It provides controlled access for reading and writing the state.
 *
 * Design principles:
 * - Single source of truth for system state
 * - Only one writer (state_manager)
 * - Read access allowed system-wide
 * - No decision logic inside this module
 *
 * Usage rules:
 * - system_state_set() MUST be called only by state_manager
 * - Other modules MUST NOT modify state directly
 *
 * @author d'oxyg
 * @date 2026
 */

#ifndef SYSTEM_STATE_STORE_H
#define SYSTEM_STATE_STORE_H

#include "system_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * TRANSITION RESULT
 * ============================================================ */

/**
 * @brief Result of a state transition attempt.
 */
typedef enum
{
    STATE_TRANSITION_OK = 0,
    STATE_TRANSITION_INVALID

} state_transition_result_t;


/* ============================================================
 * PUBLIC API
 * ============================================================ */

/**
 * @brief Initialize system state storage.
 *
 * Sets initial state to STATE_IDLE.
 * Must be called once during system startup.
 */
void system_state_init(void);


/**
 * @brief Get current system state.
 *
 * @return system_state_t Current state
 */
system_state_t system_state_get(void);


/**
 * @brief Set current system state.
 *
 * This is the ONLY function allowed to modify the system state.
 *
 * NOTE:
 * - Intended for exclusive use by state_manager
 * - Input must be a valid system_state_t value
 *
 * @param new_state New state to set
 * @return state_transition_result_t Result of operation
 */
state_transition_result_t system_state_set(system_state_t new_state);


#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_STATE_STORE_H */