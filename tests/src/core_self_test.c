/**
 * @file tests/src/core_self_test.c
 * @brief Core self‑test – implementation.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This file implements a deterministic test of the core finite state machine.
 * It registers mock command handlers, simulates sensor data events, and
 * verifies that the state machine produces the expected transitions and
 * commands.
 *
 * =============================================================================
 * TEST SEQUENCE
 * =============================================================================
 * 1. INIT → IDLE (EVENT_SYSTEM_TICK)
 * 2. IDLE → ARMED (EVENT_SYSTEM_TICK)
 * 3. ARMED → ASCENT (acceleration + velocity events)
 * 4. ASCENT → DESCENT (apogee detection)
 * 5. DESCENT → LANDED (altitude, velocity, acceleration near zero)
 *
 * =============================================================================
 * VERIFICATION
 * =============================================================================
 * - State transitions are read from system_state_get()
 * - Command executions are logged by mock handlers
 * - Expected parachute deployment is verified
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include "core_self_test.h"
#include "command_router.h"
#include "event_dispatcher.h"
#include "state_manager.h"
#include "system_state.h"
#include "event_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "CoreSelfTest";

/* ============================================================
 * MOCK COMMAND HANDLERS
 * ============================================================ */

/* Individual mock handlers for key commands */
static esp_err_t mock_arm_parachute(void *ctx, const command_param_union_t *params)
{
    (void)ctx; (void)params;
    ESP_LOGI(TAG, " -> MOCK: COMMAND_ARM_PARACHUTE");
    return ESP_OK;
}

static esp_err_t mock_deploy_parachute(void *ctx, const command_param_union_t *params)
{
    (void)ctx; (void)params;
    ESP_LOGI(TAG, " -> MOCK: COMMAND_DEPLOY_PARACHUTE (CRITICAL)");
    return ESP_OK;
}

static esp_err_t mock_start_logging(void *ctx, const command_param_union_t *params)
{
    (void)ctx; (void)params;
    ESP_LOGI(TAG, " -> MOCK: COMMAND_START_LOGGING");
    return ESP_OK;
}

static esp_err_t mock_start_telemetry(void *ctx, const command_param_union_t *params)
{
    (void)ctx; (void)params;
    ESP_LOGI(TAG, " -> MOCK: COMMAND_START_TELEMETRY");
    return ESP_OK;
}

static esp_err_t mock_system_arm(void *ctx, const command_param_union_t *params)
{
    (void)ctx; (void)params;
    ESP_LOGI(TAG, " -> MOCK: COMMAND_SYSTEM_ARM");
    return ESP_OK;
}

static esp_err_t mock_enable_gimbal(void *ctx, const command_param_union_t *params)
{
    (void)ctx; (void)params;
    ESP_LOGI(TAG, " -> MOCK: COMMAND_ENABLE_GIMBAL_CONTROL");
    return ESP_OK;
}

static esp_err_t mock_set_gimbal_quat(void *ctx, const command_param_union_t *params)
{
    (void)ctx;
    if (params) {
        const gimbal_orientation_params_t *p = (const gimbal_orientation_params_t *)params;
        ESP_LOGI(TAG, " -> MOCK: COMMAND_SET_GIMBAL_QUATERNION (q: %f %f %f %f)",
                 p->q_w, p->q_x, p->q_y, p->q_z);
    } else {
        ESP_LOGI(TAG, " -> MOCK: COMMAND_SET_GIMBAL_QUATERNION (no params)");
    }
    return ESP_OK;
}

static esp_err_t mock_stop_gimbal(void *ctx, const command_param_union_t *params)
{
    (void)ctx; (void)params;
    ESP_LOGI(TAG, " -> MOCK: COMMAND_STOP_GIMBAL_CONTROL");
    return ESP_OK;
}

static esp_err_t mock_stop_telemetry(void *ctx, const command_param_union_t *params)
{
    (void)ctx; (void)params;
    ESP_LOGI(TAG, " -> MOCK: COMMAND_STOP_TELEMETRY");
    return ESP_OK;
}

static esp_err_t mock_stop_logging(void *ctx, const command_param_union_t *params)
{
    (void)ctx; (void)params;
    ESP_LOGI(TAG, " -> MOCK: COMMAND_STOP_LOGGING");
    return ESP_OK;
}

static esp_err_t mock_flush_storage(void *ctx, const command_param_union_t *params)
{
    (void)ctx; (void)params;
    ESP_LOGI(TAG, " -> MOCK: COMMAND_FLUSH_STORAGE");
    return ESP_OK;
}

static esp_err_t mock_abort_mission(void *ctx, const command_param_union_t *params)
{
    (void)ctx; (void)params;
    ESP_LOGI(TAG, " -> MOCK: COMMAND_ABORT_MISSION");
    return ESP_OK;
}

static esp_err_t mock_generic(void *ctx, const command_param_union_t *params)
{
    (void)ctx; (void)params;
    ESP_LOGI(TAG, " -> MOCK: (generic command)");
    return ESP_OK;
}

/* ============================================================
 * HELPER: REGISTER MOCK HANDLERS
 * ============================================================ */

static void register_mock_handlers(void)
{
    command_router_register_handler(COMMAND_START_LOGGING, mock_start_logging, NULL);
    command_router_register_handler(COMMAND_START_TELEMETRY, mock_start_telemetry, NULL);
    command_router_register_handler(COMMAND_SYSTEM_ARM, mock_system_arm, NULL);
    command_router_register_handler(COMMAND_ARM_PARACHUTE, mock_arm_parachute, NULL);
    command_router_register_handler(COMMAND_ENABLE_GIMBAL_CONTROL, mock_enable_gimbal, NULL);
    command_router_register_handler(COMMAND_SET_GIMBAL_QUATERNION, mock_set_gimbal_quat, NULL);
    command_router_register_handler(COMMAND_DEPLOY_PARACHUTE, mock_deploy_parachute, NULL);
    command_router_register_handler(COMMAND_STOP_GIMBAL_CONTROL, mock_stop_gimbal, NULL);
    command_router_register_handler(COMMAND_STOP_TELEMETRY, mock_stop_telemetry, NULL);
    command_router_register_handler(COMMAND_STOP_LOGGING, mock_stop_logging, NULL);
    command_router_register_handler(COMMAND_FLUSH_STORAGE, mock_flush_storage, NULL);
    command_router_register_handler(COMMAND_ABORT_MISSION, mock_abort_mission, NULL);

    /* Register a generic handler for any remaining commands */
    for (int cmd = 0; cmd < COMMAND_COUNT; cmd++) {
        /* Skip already registered */
        if (cmd == COMMAND_START_LOGGING ||
            cmd == COMMAND_START_TELEMETRY ||
            cmd == COMMAND_SYSTEM_ARM ||
            cmd == COMMAND_ARM_PARACHUTE ||
            cmd == COMMAND_ENABLE_GIMBAL_CONTROL ||
            cmd == COMMAND_SET_GIMBAL_QUATERNION ||
            cmd == COMMAND_DEPLOY_PARACHUTE ||
            cmd == COMMAND_STOP_GIMBAL_CONTROL ||
            cmd == COMMAND_STOP_TELEMETRY ||
            cmd == COMMAND_STOP_LOGGING ||
            cmd == COMMAND_FLUSH_STORAGE ||
            cmd == COMMAND_ABORT_MISSION) {
            continue;
        }
        command_router_register_handler(cmd, mock_generic, NULL);
    }
}

/* ============================================================
 * HELPER: POST AN EVENT
 * ============================================================ */

static void post_event(event_type_t id, void *payload, size_t payload_size)
{
    event_t ev = {0};
    ev.id = id;
    ev.timestamp_us = esp_timer_get_time();
    ev.source = 0;  /* test source */

    if (payload && payload_size <= sizeof(ev.data)) {
        memcpy(&ev.data, payload, payload_size);
    }

    esp_err_t ret = event_dispatcher_post(&ev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post event %d: %d", id, ret);
    }
}

/* ============================================================
 * FLIGHT SIMULATION
 * ============================================================ */

static esp_err_t run_flight_simulation(void)
{
    ESP_LOGI(TAG, "=== Starting flight simulation ===");

    /* --------------------------------------------------------
     * 1. INIT → IDLE
     * -------------------------------------------------------- */
    ESP_LOGI(TAG, "Sending EVENT_SYSTEM_TICK");
    post_event(EVENT_SYSTEM_TICK, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    system_state_t state = system_state_get();
    ESP_LOGI(TAG, "State after tick: %s", system_state_to_string(state));
    if (state != SYSTEM_STATE_IDLE) {
        ESP_LOGE(TAG, "Expected IDLE, got %s", system_state_to_string(state));
        return ESP_FAIL;
    }

    /* --------------------------------------------------------
     * 2. IDLE → ARMED (second SYSTEM_TICK triggers arming)
     * -------------------------------------------------------- */
    ESP_LOGI(TAG, "Sending second EVENT_SYSTEM_TICK to arm");
    post_event(EVENT_SYSTEM_TICK, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    state = system_state_get();
    ESP_LOGI(TAG, "State after second tick: %s", system_state_to_string(state));
    if (state != SYSTEM_STATE_ARMED) {
        ESP_LOGE(TAG, "Expected ARMED, got %s", system_state_to_string(state));
        return ESP_FAIL;
    }

    /* --------------------------------------------------------
     * 3. ARMED → ASCENT (launch detection)
     * -------------------------------------------------------- */
    /* Velocity update first (so when acceleration arrives, velocity is valid) */
    velocity_update_t vel = {
        .value_mps = 6.0f,
        .validity = { .valid = true }
    };
    post_event(EVENT_VELOCITY_UPDATE, &vel, sizeof(vel));
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Acceleration update (high upward) */
    acceleration_data_t acc = {
        .ax_mps2 = 0.0f,
        .ay_mps2 = 0.0f,
        .az_mps2 = 15.0f,
        .validity = { .valid = true }
    };
    post_event(EVENT_ACCELERATION_UPDATE, &acc, sizeof(acc));
    vTaskDelay(pdMS_TO_TICKS(100));  /* Allow transition to process */

    state = system_state_get();
    ESP_LOGI(TAG, "State after launch: %s", system_state_to_string(state));
    if (state != SYSTEM_STATE_ASCENT) {
        ESP_LOGE(TAG, "Expected ASCENT, got %s", system_state_to_string(state));
        return ESP_FAIL;
    }

    /* --------------------------------------------------------
     * 4. ASCENT (simulate climbing)
     * -------------------------------------------------------- */
    altitude_update_t alt;
    alt.validity.valid = true;
    for (float a = 20.0f; a <= 100.0f; a += 20.0f) {
        alt.value_m = a;
        post_event(EVENT_ALTITUDE_UPDATE, &alt, sizeof(alt));
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* --------------------------------------------------------
     * 5. Apogee detection (velocity becomes negative)
     * -------------------------------------------------------- */
    vel.value_mps = -1.5f;
    post_event(EVENT_VELOCITY_UPDATE, &vel, sizeof(vel));
    vTaskDelay(pdMS_TO_TICKS(100));

    state = system_state_get();
    ESP_LOGI(TAG, "State after apogee: %s", system_state_to_string(state));
    if (state != SYSTEM_STATE_DESCENT) {
        ESP_LOGE(TAG, "Expected DESCENT, got %s", system_state_to_string(state));
        return ESP_FAIL;
    }

    /* --------------------------------------------------------
     * 6. DESCENT (simulate falling)
     * -------------------------------------------------------- */
    for (float a = 80.0f; a >= 10.0f; a -= 15.0f) {
        alt.value_m = a;
        post_event(EVENT_ALTITUDE_UPDATE, &alt, sizeof(alt));
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* --------------------------------------------------------
     * 7. Landing detection (order matters!)
     * - First update velocity and acceleration to landing values,
     * - Then post altitude update to trigger the transition.
     * -------------------------------------------------------- */
    vel.value_mps = 0.2f;
    post_event(EVENT_VELOCITY_UPDATE, &vel, sizeof(vel));
    vTaskDelay(pdMS_TO_TICKS(50));

    acc.az_mps2 = 1.0f;
    post_event(EVENT_ACCELERATION_UPDATE, &acc, sizeof(acc));
    vTaskDelay(pdMS_TO_TICKS(50));

    alt.value_m = 2.0f;
    post_event(EVENT_ALTITUDE_UPDATE, &alt, sizeof(alt));
    vTaskDelay(pdMS_TO_TICKS(100));

    state = system_state_get();
    ESP_LOGI(TAG, "State after landing: %s", system_state_to_string(state));
    if (state != SYSTEM_STATE_LANDED) {
        ESP_LOGE(TAG, "Expected LANDED, got %s", system_state_to_string(state));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "=== Flight simulation completed successfully ===");
    return ESP_OK;
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

esp_err_t core_self_test_run(void)
{
    ESP_LOGI(TAG, "Core self‑test started");

    /* 1. Initialise core modules */
    command_router_init();
    esp_err_t ret = event_dispatcher_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event_dispatcher_init failed: %d", ret);
        return ret;
    }

    /* 2. Register mock handlers (before locking) */
    register_mock_handlers();

    /* 3. Lock router */
    command_router_lock();

    /* 4. Initialise state manager (pass NULL, it will use internal context) */
    ret = state_manager_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "state_manager_init failed: %d", ret);
        return ret;
    }

    /* 5. Start event dispatcher */
    ret = event_dispatcher_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event_dispatcher_start failed: %d", ret);
        return ret;
    }

    /* 6. Run flight simulation */
    ret = run_flight_simulation();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Flight simulation failed");
        return ret;
    }

    ESP_LOGI(TAG, "Core self‑test finished successfully");
    return ESP_OK;
}