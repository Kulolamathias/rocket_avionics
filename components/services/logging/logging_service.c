/**
 * @file components/services/logging/logging_service.c
 * @brief Logging Service – implementation.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Handles COMMAND_LOG_MESSAGE by printing the message using ESP_LOG.
 * - Supports four log levels: debug, info, warn, error.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include "logging_service.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "esp_log.h"

static const char *TAG = "LOGGING";

/* Command handler */
/**
 * @brief Handle COMMAND_LOG_MESSAGE by printing the message with the appropriate log level.
 * @param context Unused.
 * @param params Pointer to command parameters, expected to contain log_message_params_t.
 * 
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if params is NULL.
 */
static esp_err_t handle_log_message(void *context, const command_param_union_t *params)
{
    (void)context;

    if (!params) {
        return ESP_ERR_INVALID_ARG;
    }

    const log_message_params_t *p = &params->log_message;

    switch (p->level) {
        case 0: /* Debug */
            ESP_LOGD(TAG, "%s", p->message);
            break;
        case 1: /* Info */
            ESP_LOGI(TAG, "%s", p->message);
            break;
        case 2: /* Warn */
            ESP_LOGW(TAG, "%s", p->message);
            break;
        case 3: /* Error */
            ESP_LOGE(TAG, "%s", p->message);
            break;
        default:
            ESP_LOGI(TAG, "%s", p->message);
            break;
    }

    return ESP_OK;
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

esp_err_t logging_service_init(void)
{
    ESP_LOGI(TAG, "Logging service initialized");
    return ESP_OK;
}

esp_err_t logging_service_register_handlers(void)
{
    return service_register_command(COMMAND_LOG_MESSAGE, handle_log_message, NULL);
}

esp_err_t logging_service_start(void)
{
    ESP_LOGI(TAG, "Logging service started");
    return ESP_OK;
}