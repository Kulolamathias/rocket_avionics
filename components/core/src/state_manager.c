/**
 * @file components/core/src/state_manager.c
 * @brief State Manager – deterministic transition engine (implementation).
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This file implements the complete behavioral specification of the system.
 * All possible system reactions are encoded in the static transition table.
 *
 * =============================================================================
 * DESIGN MODEL
 * =============================================================================
 *   STATE + EVENT + CONDITION → NEXT_STATE + COMMANDS
 *
 * - The transition table is evaluated TOP TO BOTTOM; the first matching rule wins.
 * - Conditions read ONLY from system_context_t (updated before evaluation).
 * - Events are FACTS; they never directly influence decisions (except via context).
 * - Command batches are executed strictly sequentially.
 *
 * =============================================================================
 * EXTENSIBILITY
 * =============================================================================
 * - New behavior is added by APPENDING new entries to g_transition_table.
 * - Existing rules are NEVER modified (unless correcting a bug).
 * - No dynamic rule registration – all decisions are compile-time.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include "state_manager.h"
#include "command_router.h"
#include "command_params.h"
#include "system_state.h"
#include "conditions.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "StateManager";

#define MAX_COMMANDS_PER_TRANSITION 6

/* ============================================================
 * INTERNAL TYPE DEFINITIONS – TRANSITION RULE COMPONENTS
 * ============================================================ */

/**
 * @brief Function that prepares command parameters.
 *
 * Called during command batch execution. Writes parameter data
 * into a stack-allocated union. Must be NULL if command needs no params.
 *
 * @param ctx Current system context (read-only)
 * @param event Original event (may be NULL if not event-triggered)
 * @param params_out Pointer to command_param_union_t buffer
 */
typedef void (*param_preparer_t)(const system_context_t *ctx,
                                 const event_t *event,
                                 void *params_out);

/**
 * @brief A single command inside a transition batch.
 */
typedef struct {
    command_type_t cmd;                /**< Command to execute */
    param_preparer_t prepare_params;   /**< Parameter filler (may be NULL) */
} transition_command_t;

/**
 * @brief Batch of commands to execute after a transition.
 */
typedef struct {
    transition_command_t commands[MAX_COMMANDS_PER_TRANSITION];
    uint8_t count;                     /**< Number of valid commands */
} command_batch_t;

/**
 * @brief Condition function – must return true for rule to match.
 *
 * IMPORTANT: Must read ONLY from system_context_t.
 */
typedef bool (*transition_condition_t)(const system_context_t *ctx,
                                       const event_t *event);

/**
 * @brief Complete state transition rule.
 */
typedef struct {
    system_state_t current_state;      /**< Required starting state (SYSTEM_STATE_ANY allowed) */
    event_type_t event_id;             /**< Required event */
    transition_condition_t condition;  /**< Additional predicate (may be NULL) */
    system_state_t next_state;         /**< State after transition */
    command_batch_t command_batch;     /**< Actions to execute */
} state_transition_rule_t;

/* ============================================================
 * PARAMETER PREPARERS – COMMAND DATA PACKING
 * ============================================================ */

/**
 * @brief Prepare gimbal quaternion command parameters.
 *
 * Copies the current orientation from context into the command parameters.
 * Only executes if orientation data is valid.
 */
static void prepare_gimbal_quaternion(const system_context_t *ctx,
                                      const event_t *event,
                                      void *params_out)
{
    (void)event;
    gimbal_orientation_params_t *p = (gimbal_orientation_params_t *)params_out;

    /* Safety: only prepare if orientation is valid */
    if (!ctx->orientation_valid) {
        /* Log warning but still zero the parameters (safe default) */
        ESP_LOGW(TAG, "Attempting to prepare gimbal command with invalid orientation");
        memset(p, 0, sizeof(gimbal_orientation_params_t));
        return;
    }

    /* Copy current orientation from context */
    p->q_w = ctx->orientation.q_w;
    p->q_x = ctx->orientation.q_x;
    p->q_y = ctx->orientation.q_y;
    p->q_z = ctx->orientation.q_z;
}

/**
 * @brief Prepare telemetry start command parameters.
 */
static void prepare_telemetry_start(const system_context_t *ctx,
                                    const event_t *event,
                                    void *params_out)
{
    (void)ctx;
    (void)event;
    telemetry_config_params_t *p = (telemetry_config_params_t *)params_out;

    /* Default telemetry interval: 100 ms (10 Hz) */
    p->interval_ms = 100;
}

/**
 * @brief Prepare logging start command parameters.
 */
static void prepare_logging_start(const system_context_t *ctx,
                                  const event_t *event,
                                  void *params_out)
{
    (void)ctx;
    (void)event;
    logging_config_params_t *p = (logging_config_params_t *)params_out;

    /* Default sampling rate: 50 Hz */
    p->sampling_rate_hz = 50;
}

static void prepare_log_mqtt_message(const system_context_t *ctx,
                                     const event_t *event,
                                     void *params_out)
{
    (void)ctx;
    log_message_params_t *p = (log_message_params_t *)params_out;
    p->level = 1; /* Info level */

    const mqtt_message_t *msg = &event->data.mqtt_message;
    snprintf(p->message, sizeof(p->message),
             "MQTT: %.*s",
             (int)msg->payload_len,
             (const char *)msg->payload);
}

/* Prepare log message from ultrasonic data */
static void prepare_log_ultrasonic(const system_context_t *ctx,
                                   const event_t *event,
                                   void *params_out)
{
    (void)ctx;
    log_message_params_t *p = (log_message_params_t *)params_out;
    p->level = 1; /* Info level */
    const ultrasonic_data_t *data = &event->data.ultrasonic;
    snprintf(p->message, sizeof(p->message),
             "Ultrasonic: distance=%lu cm, fill=%u%%",
             data->distance_cm, data->fill_percent);
}


/* ============================================================
 * TRANSITION TABLE – SINGLE SOURCE OF TRUTH
 * ============================================================ */

/**
 * @brief Static transition table.
 *
 * EVALUATION RULES:
 * 1. Rules are evaluated in the order they appear in this table.
 * 2. The FIRST rule where (current_state matches, event_id matches, condition passes) is applied.
 * 3. No other rules are considered after a match.
 * 4. If no rule matches, the event is ignored (logged).
 *
 * This is intentional and deterministic.
 */
static const state_transition_rule_t g_transition_table[] =
{
    /* --------------------------------------------------------
     * STATE: INIT → IDLE (after first system tick)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_INIT,
        .event_id = EVENT_SYSTEM_TICK,
        .condition = NULL,
        .next_state = SYSTEM_STATE_IDLE,
        .command_batch = {
            .commands = {
                { COMMAND_START_LOGGING, prepare_logging_start },
                { COMMAND_START_TELEMETRY, prepare_telemetry_start }
            },
            .count = 2
        }
    },

    /* --------------------------------------------------------
     * STATE: IDLE → ARMED (arming command via tick – can be replaced with explicit arm)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_IDLE,
        .event_id = EVENT_SYSTEM_TICK,
        .condition = NULL,  /* Replace with actual arming condition if needed */
        .next_state = SYSTEM_STATE_ARMED,
        .command_batch = {
            .commands = {
                { COMMAND_SYSTEM_ARM, NULL },
                { COMMAND_ARM_PARACHUTE, NULL }
            },
            .count = 2
        }
    },

    /* --------------------------------------------------------
     * STATE: ARMED → ASCENT (LAUNCH DETECTED)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_ARMED,
        .event_id = EVENT_ACCELERATION_UPDATE,
        .condition = cond_launch_detected,
        .next_state = SYSTEM_STATE_ASCENT,
        .command_batch = {
            .commands = {
                { COMMAND_ENABLE_GIMBAL_CONTROL, NULL },
                { COMMAND_SET_GIMBAL_QUATERNION, prepare_gimbal_quaternion }
            },
            .count = 2
        }
    },

    /* --------------------------------------------------------
     * STATE: ASCENT → DESCENT (APOGEE DETECTED)
     * CRITICAL: parachute deployment
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_ASCENT,
        .event_id = EVENT_VELOCITY_UPDATE,
        .condition = cond_apogee_detected,
        .next_state = SYSTEM_STATE_DESCENT,
        .command_batch = {
            .commands = {
                { COMMAND_DEPLOY_PARACHUTE, NULL },
                { COMMAND_STOP_GIMBAL_CONTROL, NULL }
            },
            .count = 2
        }
    },

    /* --------------------------------------------------------
     * STATE: DESCENT → LANDED (LANDING DETECTED)
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_DESCENT,
        .event_id = EVENT_ALTITUDE_UPDATE,
        .condition = cond_landed,
        .next_state = SYSTEM_STATE_LANDED,
        .command_batch = {
            .commands = {
                { COMMAND_STOP_TELEMETRY, NULL },
                { COMMAND_STOP_LOGGING, NULL },
                { COMMAND_FLUSH_STORAGE, NULL }
            },
            .count = 3
        }
    },

    /* --------------------------------------------------------
     * ANY STATE → ERROR (SENSOR FAILURE)
     * This single rule covers all states because current_state = SYSTEM_STATE_ANY.
     * -------------------------------------------------------- */
    {
        .current_state = SYSTEM_STATE_ANY,
        .event_id = EVENT_SENSOR_FAILURE,
        .condition = NULL,
        .next_state = SYSTEM_STATE_ERROR,
        .command_batch = {
            .commands = {
                { COMMAND_STOP_GIMBAL_CONTROL, NULL },
                { COMMAND_ABORT_MISSION, NULL }
            },
            .count = 2
        }
    },
    {
        .current_state = SYSTEM_STATE_ANY,
        .event_id = EVENT_NETWORK_MESSAGE_RECEIVED,
        .condition = NULL,
        .next_state = SYSTEM_STATE_ANY,
        .command_batch = {
            .commands = {
                { COMMAND_LOG_MESSAGE, prepare_log_mqtt_message }
            },
            .count = 1
        }
    },
        /* ANY STATE → ANY STATE (trigger ultrasonic read on periodic timer) */
    {
        .current_state = SYSTEM_STATE_ANY,
        .event_id = EVENT_PERIODIC_TIMER_EXPIRED,
        .condition = NULL,
        .next_state = SYSTEM_STATE_ANY,
        .command_batch = {
            .commands = { { COMMAND_READ_ULTRASONIC, NULL } },
            .count = 1
        }
    },

    /* ANY STATE → ANY STATE (log ultrasonic data) */
    {
        .current_state = SYSTEM_STATE_ANY,
        .event_id = EVENT_ULTRASONIC_DATA,
        .condition = NULL,
        .next_state = SYSTEM_STATE_ANY,
        .command_batch = {
            .commands = { { COMMAND_LOG_MESSAGE, prepare_log_ultrasonic } },
            .count = 1
        }
    },
};

#define TRANSITION_TABLE_SIZE (sizeof(g_transition_table) / sizeof(g_transition_table[0]))

/* ============================================================
 * INTERNAL HELPER: EXECUTE COMMAND BATCH
 * ============================================================ */

static void execute_command_batch(const command_batch_t *batch, const event_t *event)
{
    command_param_union_t param_buffer;
    const system_context_t *ctx = system_context_get();

    for (uint8_t i = 0; i < batch->count; i++) {
        const transition_command_t *tc = &batch->commands[i];
        void *params = NULL;

        if (tc->prepare_params != NULL) {
            memset(&param_buffer, 0, sizeof(param_buffer));
            tc->prepare_params(ctx, event, &param_buffer);
            params = &param_buffer;
        }

        esp_err_t ret = command_router_execute(tc->cmd, params);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Command %d execution returned error 0x%x", tc->cmd, ret);
        }
    }
}

/* ============================================================
 * PUBLIC API: INITIALIZATION
 * ============================================================ */

esp_err_t state_manager_init(const system_context_t *initial_context)
{
    (void)initial_context;  /* Not used – context is now internally initialized */

    /* Initialize the system context (singleton) */
    system_context_init();

    /* Initialize system state module */
    system_state_init();

    ESP_LOGI(TAG, "State manager initialized. Current state: %s",
             system_state_to_string(system_state_get()));
    return ESP_OK;
}

/* ============================================================
 * PUBLIC API: EVENT PROCESSING
 * ============================================================ */

esp_err_t state_manager_process_event(const event_t *event)
{
    ESP_LOGI(TAG, "Processing event %d", event->id);
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 1. UPDATE CONTEXT – absorb observed facts */
    system_context_update(event);

    /* 2. GET CURRENT SYSTEM STATE */
    system_state_t current_state = system_state_get();

    /* 3. EVALUATE TRANSITION TABLE – first match wins */
    for (uint32_t i = 0; i < TRANSITION_TABLE_SIZE; i++) {
        const state_transition_rule_t *rule = &g_transition_table[i];

        /* Check state (allow ANY) */
        if (rule->current_state != current_state &&
            rule->current_state != SYSTEM_STATE_ANY) {
            continue;
        }

        /* Check event */
        if (rule->event_id != event->id) {
            continue;
        }

        /* Check condition (if present) */
        if (rule->condition != NULL) {
            const system_context_t *ctx = system_context_get();
            if (!rule->condition(ctx, event)) {
                continue;
            }
        }

        /* Transition matches – execute commands and change state */
        ESP_LOGI(TAG, "Transition: %s → %s (event %d)",
                 system_state_to_string(current_state),
                 system_state_to_string(rule->next_state),
                 event->id);

        execute_command_batch(&rule->command_batch, event);
        if (rule->next_state != SYSTEM_STATE_ANY) {
            system_state_set(rule->next_state);
        }
        return ESP_OK;
    }

    if (event->id == EVENT_NETWORK_MESSAGE_RECEIVED) {
        ESP_LOGI(TAG, "MQTT received: \n\t topic=%.*s, \n\t payload=%.*s",
                (int)strlen(event->data.mqtt_message.topic), event->data.mqtt_message.topic,
                (int)event->data.mqtt_message.payload_len, event->data.mqtt_message.payload);
    }

    /* No transition matched – ignore event */
    ESP_LOGD(TAG, "No transition for event %d in state %s",
             event->id, system_state_to_string(current_state));
    return ESP_OK;
}

/* ============================================================
 * PUBLIC API: CONTEXT ACCESS
 * ============================================================ */

const system_context_t* state_manager_get_context(void)
{
    return system_context_get();
}