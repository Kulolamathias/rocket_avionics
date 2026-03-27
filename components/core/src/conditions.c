/**
 * @file components/core/src/conditions.c
 * @brief Flight condition evaluation functions (implementation).
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Implements the condition functions declared in conditions.h. These pure
 * functions evaluate the current system context (and optionally the event)
 * to determine if a state transition should occur.
 *
 * =============================================================================
 * DESIGN PRINCIPLES
 * =============================================================================
 * - No side effects: no modification of context, events, or system state.
 * - Deterministic: same inputs always produce same output.
 * - Validity checks: all conditions verify that required data is valid.
 *
 * =============================================================================
 * THRESHOLD VALUES (for 10 km rocket, 4.5–7 m length)
 * =============================================================================
 * - Launch detection:   accel > 12 m/s², velocity > 5 m/s
 * - Ascent confirmation: velocity > 0, altitude > 10 m
 * - Apogee detection:   velocity < -1 m/s, altitude > 50 m
 * - Descent confirmation: velocity < -2 m/s
 * - Landing:            altitude < 3 m, |velocity| < 0.5 m/s, accel < 1.5 m/s²
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include "conditions.h"
#include "esp_log.h"

static const char *TAG = "conditions";

/* ============================================================
 * LAUNCH DETECTION
 * ============================================================ */

bool cond_launch_detected(const system_context_t *ctx, const event_t *event)
{
    (void)event;  /* Not used for this condition */

    /* Need valid acceleration and velocity data */
    if (!ctx->acceleration_valid || !ctx->velocity_valid) {
        return false;
    }

    /* Launch: high upward acceleration and positive velocity */
    return (ctx->acceleration_z_mps2 > 12.0f && ctx->velocity_z_mps > 5.0f);
}

/* ============================================================
 * ASCENT CONFIRMATION
 * ============================================================ */

bool cond_ascent_confirmed(const system_context_t *ctx, const event_t *event)
{
    (void)event;

    /* Need valid altitude and velocity */
    if (!ctx->altitude_valid || !ctx->velocity_valid) {
        return false;
    }

    /* Ascent: positive velocity and altitude above 10 meters */
    return (ctx->velocity_z_mps > 0.0f && ctx->altitude_m > 10.0f);
}

/* ============================================================
 * APOGEE DETECTION
 * ============================================================ */

bool cond_apogee_detected(const system_context_t *ctx, const event_t *event)
{
    (void)event;

    /* Need valid velocity and altitude */
    if (!ctx->velocity_valid || !ctx->altitude_valid) {
        return false;
    }

    /* Apogee: negative velocity (descending) and altitude above 50 m */
    return (ctx->velocity_z_mps < -1.0f && ctx->altitude_m > 50.0f);
}

/* ============================================================
 * DESCENT CONFIRMATION
 * ============================================================ */

bool cond_descent_confirmed(const system_context_t *ctx, const event_t *event)
{
    (void)event;

    /* Need valid velocity */
    if (!ctx->velocity_valid) {
        return false;
    }

    /* Descent: significant downward velocity */
    return (ctx->velocity_z_mps < -2.0f);
}

/* ============================================================
 * LANDING DETECTION
 * ============================================================ */

bool cond_landed(const system_context_t *ctx, const event_t *event)
{
    (void)event;

    /* Need valid altitude, velocity, and acceleration */
    if (!ctx->altitude_valid || !ctx->velocity_valid || !ctx->acceleration_valid) {
        return false;
    }

    /* Landing: near ground, near zero velocity, low acceleration */
    return (ctx->altitude_m < 3.0f &&
            ctx->velocity_z_mps < 0.5f && ctx->velocity_z_mps > -0.5f &&
            ctx->acceleration_z_mps2 < 1.5f);
}