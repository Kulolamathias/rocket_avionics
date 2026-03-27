/**
 * @file components/core/src/system_state.c
 * @brief System state authority – implementation.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Implements the single authoritative storage of the system state.
 * This file contains absolutely no decision logic – it is a passive record keeper.
 *
 * =============================================================================
 * INVARIANTS
 * =============================================================================
 * - s_current_state is never read or written outside this module.
 * - s_current_state is always a valid system_state_t.
 * - No other module shadows, caches, or duplicates the state value.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include "system_state.h"
#include "esp_log.h"

static const char *TAG = "system_state";

/* ============================================================
 * SINGLE AUTHORITATIVE INSTANCE
 * ============================================================ */

static system_state_t s_current_state = SYSTEM_STATE_INIT;

/* ============================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================ */

void system_state_init(void)
{
    s_current_state = SYSTEM_STATE_INIT;
    ESP_LOGI(TAG, "State initialized: %s", system_state_to_string(s_current_state));
}

system_state_t system_state_get(void)
{
    return s_current_state;
}

state_transition_result_t system_state_set(system_state_t new_state)
{
    if (new_state >= SYSTEM_STATE_MAX) {
        ESP_LOGE(TAG, "Invalid state transition attempt: %d", new_state);
        return STATE_TRANSITION_INVALID;
    }

    system_state_t old_state = s_current_state;
    s_current_state = new_state;

    ESP_LOGI(TAG, "State transition: %s → %s",
             system_state_to_string(old_state),
             system_state_to_string(new_state));

    return STATE_TRANSITION_OK;
}

const char* system_state_to_string(system_state_t state)
{
    switch (state) {
        case SYSTEM_STATE_INIT:     return "INIT";
        case SYSTEM_STATE_IDLE:     return "IDLE";
        case SYSTEM_STATE_ARMED:    return "ARMED";
        case SYSTEM_STATE_ASCENT:   return "ASCENT";
        case SYSTEM_STATE_DESCENT:  return "DESCENT";
        case SYSTEM_STATE_LANDED:   return "LANDED";
        case SYSTEM_STATE_ERROR:    return "ERROR";
        default:                    return "UNKNOWN";
    }
}