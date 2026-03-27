/**
 * @file components/core/include/conditions.h
 * @brief Flight condition evaluation functions.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Encapsulates all condition logic used by state_manager during transition
 * evaluation. Conditions are pure functions that determine whether a
 * transition should occur based on the current system context and optionally
 * the incoming event.
 *
 * =============================================================================
 * DESIGN PRINCIPLES
 * =============================================================================
 * - No side effects: conditions do NOT modify context, events, or system state.
 * - Read-only access: conditions only read from system_context_t.
 * - Deterministic: same inputs always produce same output.
 * - Event parameter may be used for context-free conditions (e.g., event type)
 *   but typically conditions rely on updated context.
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 * These functions are referenced in the transition table (state_manager.c).
 * They are called only when the current state and event match a rule.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef CONDITIONS_H
#define CONDITIONS_H

#include <stdbool.h>
#include "system_context.h"
#include "event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * FLIGHT PHASE CONDITIONS
 * ============================================================ */

/**
 * @brief Detect rocket launch (liftoff).
 *
 * Conditions: vertical acceleration > 12 m/s² and vertical velocity > 5 m/s.
 *
 * @param ctx Current system context (read-only)
 * @param event Incoming event (may be used for event-specific checks)
 * @return true if launch is detected
 */
bool cond_launch_detected(const system_context_t *ctx,
                          const event_t *event);

/**
 * @brief Confirm ascent phase.
 *
 * Conditions: vertical velocity > 0 and altitude > 10 m.
 *
 * @param ctx Current system context
 * @param event Incoming event
 * @return true if ascent is confirmed
 */
bool cond_ascent_confirmed(const system_context_t *ctx,
                           const event_t *event);

/**
 * @brief Detect apogee (peak altitude).
 *
 * Conditions: vertical velocity < -1 m/s and altitude > 50 m.
 *
 * @param ctx Current system context
 * @param event Incoming event
 * @return true if apogee is detected
 */
bool cond_apogee_detected(const system_context_t *ctx,
                          const event_t *event);

/**
 * @brief Confirm descent phase.
 *
 * Conditions: vertical velocity < -2 m/s.
 *
 * @param ctx Current system context
 * @param event Incoming event
 * @return true if descent is confirmed
 */
bool cond_descent_confirmed(const system_context_t *ctx,
                            const event_t *event);

/**
 * @brief Detect landing.
 *
 * Conditions: altitude < 3 m, vertical velocity within ±0.5 m/s,
 *             acceleration < 1.5 m/s².
 *
 * @param ctx Current system context
 * @param event Incoming event
 * @return true if landed
 */
bool cond_landed(const system_context_t *ctx,
                 const event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* CONDITIONS_H */