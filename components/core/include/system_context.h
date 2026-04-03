/**
 * @file components/core/include/system_context.h
 * @brief System context – unified state of observed system variables.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This module holds all processed observations required for decision-making.
 * It is the SINGLE SOURCE OF TRUTH for the system's current measured state.
 *
 * - Context is updated by state_manager based on incoming events.
 * - Conditions evaluate context to determine transitions.
 * - Context is read-only for conditions and services.
 *
 * =============================================================================
 * DESIGN PRINCIPLES
 * =============================================================================
 * - Singleton pattern: one static context instance, never exposed directly.
 * - Updated only by system_context_update() called from state_manager.
 * - Never modified by services or conditions.
 * - Validity flags prevent use of stale or invalid data.
 *
 * =============================================================================
 * DATA FLOW
 * =============================================================================
 *   Event → state_manager → system_context_update() → context → conditions → transition
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef SYSTEM_CONTEXT_H
#define SYSTEM_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>
#include "event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * ORIENTATION STRUCTURE
 * ============================================================ */

/**
 * @brief Combined orientation representation.
 *
 * Quaternion is primary for control to avoid gimbal lock.
 * Euler angles are provided for telemetry and debugging.
 */
typedef struct
{
    /* Quaternion (primary for control) */
    float q_w;          /**< Quaternion w component */
    float q_x;          /**< Quaternion x component */
    float q_y;          /**< Quaternion y component */
    float q_z;          /**< Quaternion z component */

    /* Euler angles (for telemetry/debug) */
    float roll_rad;     /**< Roll angle in radians */
    float pitch_rad;    /**< Pitch angle in radians */
    float yaw_rad;      /**< Yaw angle in radians */

    data_validity_t validity; /**< Validity flag */
} orientation_t;

/* ============================================================
 * SYSTEM CONTEXT STRUCTURE
 * ============================================================ */

/**
 * @brief Central context structure – holds all observed facts.
 */
typedef struct
{
    char mac_str[13];           /**< Device MAC address as lowercase hex string (without separators) */
    /* Time */
    uint64_t timestamp_us;          /**< Timestamp of last context update */

    /* Kinematics (primary flight data) */
    float altitude_m;               /**< Filtered/derived altitude in meters */
    float velocity_z_mps;           /**< Vertical velocity in m/s (positive up) */
    float acceleration_z_mps2;      /**< Vertical acceleration in m/s² (positive up) */
    bool altitude_valid;            /**< True if altitude is valid */
    bool velocity_valid;            /**< True if velocity is valid */
    bool acceleration_valid;        /**< True if acceleration is valid */

    /* Orientation (quaternion + Euler) */
    orientation_t orientation;      /**< Current orientation */
    bool orientation_valid;         /**< True if orientation is valid */

    /* GPS (telemetry only) */
    double latitude;                /**< Latitude in degrees (positive north) */
    double longitude;               /**< Longitude in degrees (positive east) */
    float gps_altitude_m;           /**< GPS altitude in meters */
    bool gps_valid;                 /**< True if GPS fix is usable */

    /* Power (for future use) */
    float battery_voltage_v;        /**< Battery voltage in Volts */
    bool battery_valid;             /**< True if voltage reading is valid */

    /* Fault flags */
    uint32_t fault_flags;           /**< Bitmask of active faults */

} system_context_t;

/* ============================================================
 * INITIALIZATION
 * ============================================================ */

/**
 * @brief Initialize the system context singleton.
 *
 * Sets all values to zero, all validity flags to false.
 * Must be called once during system startup.
 */
void system_context_init(void);

/* ============================================================
 * UPDATE FROM EVENT
 * ============================================================ */

/**
 * @brief Update the system context based on an incoming event.
 *
 * This function is called by state_manager during event processing.
 * It extracts data from the event and updates the corresponding fields
 * in the singleton context.
 *
 * @param event Pointer to event structure.
 */
void system_context_update(const event_t *event);

/* ============================================================
 * ACCESS (READ-ONLY)
 * ============================================================ */

/**
 * @brief Get read-only pointer to the system context.
 *
 * @return const pointer to the singleton system context.
 */
const system_context_t* system_context_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_CONTEXT_H */