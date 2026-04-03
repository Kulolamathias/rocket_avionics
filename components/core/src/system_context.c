/**
 * @file components/core/src/system_context.c
 * @brief System context – unified state of observed system variables (implementation).
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Implements the singleton system context that holds all processed observations
 * required for decision-making. This is the SINGLE SOURCE OF TRUTH for the
 * system's current measured state.
 *
 * - Context is updated exclusively by state_manager via system_context_update().
 * - Conditions and param preparers read context via system_context_get().
 * - No other module may modify or shadow the context.
 *
 * =============================================================================
 * DATA FLOW
 * =============================================================================
 *   Event → state_manager → system_context_update() → static context
 *
 * =============================================================================
 * THREAD SAFETY
 * =============================================================================
 * - Context updates occur only in the event_dispatcher task context.
 * - No locks needed because all updates are serialized through the event queue.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include "system_context.h"
#include "esp_log.h"
#include "esp_efuse.h"
#include "esp_mac.h"
#include <string.h>

static const char *TAG = "system_context";

/* ============================================================
 * SINGLETON CONTEXT
 * ============================================================ */

static system_context_t s_ctx;

/* ============================================================
 * INITIALIZATION
 * ============================================================ */

void system_context_init(void)
{
    memset(&s_ctx, 0, sizeof(system_context_t));

    /* All validity flags start false */
    s_ctx.altitude_valid = false;
    s_ctx.velocity_valid = false;
    s_ctx.acceleration_valid = false;
    s_ctx.orientation_valid = false;
    s_ctx.gps_valid = false;
    s_ctx.battery_valid = false;

    memset(&s_ctx, 0, sizeof(system_context_t));
    /* ... existing validity flags ... */

    /* Get MAC address */
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_ctx.mac_str, sizeof(s_ctx.mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "\n\tAssigned mac_str: %s", s_ctx.mac_str);

    ESP_LOGI(TAG, "System context initialized");
}

/* ============================================================
 * UPDATE FROM EVENT
 * ============================================================ */

void system_context_update(const event_t *event)
{
    if (event == NULL) {
        return;
    }

    /* Always update timestamp from event */
    s_ctx.timestamp_us = event->timestamp_us;

    /* Update fields based on event type */
    switch (event->id) {
        /* --------------------------------------------------------
         * Kinematics
         * -------------------------------------------------------- */
        case EVENT_ALTITUDE_UPDATE:
            s_ctx.altitude_m = event->data.altitude.value_m;
            s_ctx.altitude_valid = event->data.altitude.validity.valid;
            break;

        case EVENT_VELOCITY_UPDATE:
            s_ctx.velocity_z_mps = event->data.velocity.value_mps;
            s_ctx.velocity_valid = event->data.velocity.validity.valid;
            break;

        case EVENT_ACCELERATION_UPDATE:
            s_ctx.acceleration_z_mps2 = event->data.acceleration.az_mps2;
            s_ctx.acceleration_valid = event->data.acceleration.validity.valid;
            break;

        /* --------------------------------------------------------
         * Orientation (quaternion + Euler)
         * -------------------------------------------------------- */
        case EVENT_ORIENTATION_UPDATE:
            s_ctx.orientation.q_w = event->data.orientation.q_w;
            s_ctx.orientation.q_x = event->data.orientation.q_x;
            s_ctx.orientation.q_y = event->data.orientation.q_y;
            s_ctx.orientation.q_z = event->data.orientation.q_z;
            s_ctx.orientation.roll_rad = event->data.orientation.roll_rad;
            s_ctx.orientation.pitch_rad = event->data.orientation.pitch_rad;
            s_ctx.orientation.yaw_rad = event->data.orientation.yaw_rad;
            s_ctx.orientation.validity = event->data.orientation.validity;
            s_ctx.orientation_valid = event->data.orientation.validity.valid;
            break;

        /* --------------------------------------------------------
         * GPS (telemetry only)
         * -------------------------------------------------------- */
        case EVENT_GPS_UPDATE:
            s_ctx.latitude = event->data.gps.latitude;
            s_ctx.longitude = event->data.gps.longitude;
            s_ctx.gps_altitude_m = event->data.gps.altitude_m;
            s_ctx.gps_valid = event->data.gps.validity.valid;
            break;

        case EVENT_GPS_ALTITUDE_UPDATE:
            s_ctx.gps_altitude_m = event->data.gps_altitude.value_m;
            s_ctx.gps_valid = event->data.gps_altitude.validity.valid;
            break;

        /* --------------------------------------------------------
         * Power
         * -------------------------------------------------------- */
        case EVENT_BATTERY_VOLTAGE_UPDATE:
            s_ctx.battery_voltage_v = event->data.battery_voltage.voltage_v;
            s_ctx.battery_valid = event->data.battery_voltage.validity.valid;
            break;

        /* --------------------------------------------------------
         * Sensor failure – set fault flag
         * -------------------------------------------------------- */
        case EVENT_SENSOR_FAILURE:
            s_ctx.fault_flags |= (1 << event->data.sensor_failure.sensor_id);
            break;

        /* --------------------------------------------------------
         * Other events: no context update needed
         * -------------------------------------------------------- */
        default:
            /* Ignore events that do not affect context */
            break;
    }
}

/* ============================================================
 * ACCESS (READ-ONLY)
 * ============================================================ */

const system_context_t* system_context_get(void)
{
    return &s_ctx;
}