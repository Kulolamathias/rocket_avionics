/**
 * @file components/core/src/command_router.c
 * @brief Command Router – implementation (controlled registration model).
 *
 * =============================================================================
 * INTERNAL DESIGN
 * =============================================================================
 * - Static registry indexed by command_type_t.
 * - Each command has exactly one handler entry.
 * - Registry is mutable ONLY during registration phase.
 *
 * =============================================================================
 * STATE MACHINE (INTERNAL)
 * =============================================================================
 *   INIT → REGISTER → LOCKED → RUN
 *
 * =============================================================================
 * SAFETY
 * =============================================================================
 * - Bounds checking on command IDs.
 * - No overwrite of registered handlers.
 * - No execution before lock.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include "command_router.h"
#include "esp_log.h"

static const char *TAG = "command_router";

/* ============================================================
 * INTERNAL TYPES
 * ============================================================ */

/**
 * @brief Internal command registry entry.
 */
typedef struct
{
    command_handler_fn_t handler;   /**< Handler function */
    void *context;                  /**< Service context */
    bool registered;                /**< Registration flag */
} command_entry_t;

/* ============================================================
 * INTERNAL STATE
 * ============================================================ */

static command_entry_t s_registry[COMMAND_COUNT];
static bool s_locked = false;

/* ============================================================
 * HELPER FUNCTIONS
 * ============================================================ */

static bool is_valid_command(command_type_t command)
{
    return (command >= 0 && command < COMMAND_COUNT);
}

/* ============================================================
 * INIT PHASE
 * ============================================================ */

void command_router_init(void)
{
    /* Clear registry */
    for (uint32_t i = 0; i < COMMAND_COUNT; i++) {
        s_registry[i].handler = NULL;
        s_registry[i].context = NULL;
        s_registry[i].registered = false;
    }
    s_locked = false;
    ESP_LOGI(TAG, "Command router initialized (registry empty)");
}

/* ============================================================
 * REGISTER PHASE
 * ============================================================ */

esp_err_t command_router_register_handler(command_type_t command,
                                          command_handler_fn_t handler,
                                          void *context)
{
    if (!is_valid_command(command)) {
        ESP_LOGE(TAG, "Invalid command id: %d", command);
        return ESP_ERR_INVALID_ARG;
    }

    if (handler == NULL) {
        ESP_LOGE(TAG, "NULL handler for command %d", command);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_locked) {
        ESP_LOGE(TAG, "Cannot register after lock (command %d)", command);
        return ESP_ERR_INVALID_STATE;
    }

    command_entry_t *entry = &s_registry[command];
    if (entry->registered) {
        ESP_LOGE(TAG, "Handler already registered for command %d", command);
        return ESP_ERR_INVALID_STATE;
    }

    entry->handler = handler;
    entry->context = context;
    entry->registered = true;

    ESP_LOGD(TAG, "Registered handler for command %d", command);
    return ESP_OK;
}

/* ============================================================
 * LOCK PHASE
 * ============================================================ */

void command_router_lock(void)
{
    s_locked = true;
    ESP_LOGI(TAG, "Command router locked");
}

bool command_router_is_locked(void)
{
    return s_locked;
}

/* ============================================================
 * RUN PHASE
 * ============================================================ */

esp_err_t command_router_execute(command_type_t command,
                                 const command_param_union_t *params)
{
    if (!is_valid_command(command)) {
        ESP_LOGE(TAG, "Invalid command id: %d", command);
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_locked) {
        ESP_LOGE(TAG, "Cannot execute command %d before router is locked", command);
        return ESP_ERR_INVALID_STATE;
    }

    command_entry_t *entry = &s_registry[command];
    if (!entry->registered || entry->handler == NULL) {
        ESP_LOGW(TAG, "No handler registered for command %d", command);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = entry->handler(entry->context, params);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Command %d handler returned error 0x%x", command, ret);
    }
    return ret;
}