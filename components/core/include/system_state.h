/**
 * @file components/core/include/system_state.h
 * @brief System state authority – single source of truth for operational state.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This module is the sole owner of the current system state. No other module
 * stores, caches, or modifies the system state independently.
 *
 * State transitions are performed exclusively by state_manager.c, which calls
 * system_state_set(). All other modules read the state via system_state_get().
 *
 * =============================================================================
 * DESIGN PRINCIPLES
 * =============================================================================
 * - Single authoritative storage (static variable)
 * - No decision logic – pure storage and retrieval
 * - State transitions are controlled (set function returns result)
 * - Read access is unrestricted; write access is restricted
 *
 * =============================================================================
 * STATE ENUMERATION
 * =============================================================================
 * These states represent the stable operational modes of the rocket avionics.
 * Transitions between states are defined in the transition table (state_manager.c).
 *
 * SYSTEM_STATE_ANY is a special sentinel used in transition rules to match
 * any state, avoiding duplication of rules across states.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * STATE ENUMERATION
 * ============================================================ */

/**
 * @brief System operational states.
 */
typedef enum
{
    SYSTEM_STATE_INIT = 0,      /**< System starting, hardware not ready */
    SYSTEM_STATE_IDLE,          /**< Idle, waiting for arming command */
    SYSTEM_STATE_ARMED,         /**< Armed, ready for launch detection */
    SYSTEM_STATE_ASCENT,        /**< Powered ascent phase */
    SYSTEM_STATE_DESCENT,       /**< Descent phase (after apogee) */
    SYSTEM_STATE_LANDED,        /**< Rocket landed, safe */
    SYSTEM_STATE_ERROR,         /**< Fault condition – safe fallback */
    SYSTEM_STATE_MAX            /**< Sentinel – number of states */
} system_state_t;

/* Special value to match any state in transition rules */
#define SYSTEM_STATE_ANY SYSTEM_STATE_MAX

/* ============================================================
 * TRANSITION RESULT
 * ============================================================ */

/**
 * @brief Result of a state transition attempt.
 */
typedef enum
{
    STATE_TRANSITION_OK = 0,    /**< State changed successfully */
    STATE_TRANSITION_INVALID    /**< New state out of range or invalid */
} state_transition_result_t;

/* ============================================================
 * PUBLIC API
 * ============================================================ */

/**
 * @brief Initialize the system state module.
 *
 * Sets the authoritative state to SYSTEM_STATE_INIT.
 * Must be called once before any state read/write.
 */
void system_state_init(void);

/**
 * @brief Get the current system state.
 *
 * @return system_state_t Current state.
 */
system_state_t system_state_get(void);

/**
 * @brief Set the current system state.
 *
 * This function is the only way to change the system state.
 * Intended for exclusive use by state_manager.c.
 *
 * @param new_state Valid state (must be < SYSTEM_STATE_MAX).
 * @return STATE_TRANSITION_OK if changed, STATE_TRANSITION_INVALID if out of range.
 */
state_transition_result_t system_state_set(system_state_t new_state);

/**
 * @brief Convert state to human-readable string (for logging).
 *
 * @param state State to convert.
 * @return Constant string representation.
 */
const char* system_state_to_string(system_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_STATE_H */