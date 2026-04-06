/**
 * @file components/services/communication/rocket_command_service/rocket_command_service.c
 * @brief Rocket Command Service – implementation.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Uses a private command queue (internal) to decouple MQTT event handling
 *   from command execution. However, to keep it simple, we directly execute
 *   commands from the MQTT event callback (which is already in a task context).
 * - JSON parsing uses cJSON.
 * - The service subscribes to the command topic in its start function.
 * =============================================================================
 */

#include "rocket_command_service.h"
#include "service_interfaces.h"
#include "command_router.h"
#include "command_params.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_efuse.h"
#include "esp_mac.h"
#include <string.h>

static const char *TAG = "RKT_CMD_SVC";

/* Helper: extract command name from topic */
static const char* extract_command_from_topic(const char *topic)
{
    /* Topic format: rocket/<mac>/cmd/<command> */
    const char *cmd_start = strrchr(topic, '/');
    if (!cmd_start) return NULL;
    return cmd_start + 1;
}

/* Main MQTT message handler – called via command router from state_manager */
static esp_err_t handle_rocket_command(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    const web_command_params_t *cmd = &params->web_cmd;
    const char *topic = cmd->topic;
    const char *payload = (const char*)cmd->payload;
    size_t len = cmd->payload_len;

    /* Check if the topic matches rocket/<mac>/cmd/... */
    const char *command = extract_command_from_topic(topic);
    if (!command) {
        /* Not a rocket command topic; ignore */
        return ESP_OK;
    }

    /* Parse JSON */
    cJSON *root = cJSON_ParseWithLength(payload, len);
    if (!root) {
        ESP_LOGE(TAG, "Invalid JSON payload for command %s", command);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    command_param_union_t cmd_params = {0};

    if (strcmp(command, "ignition") == 0) {
        cJSON *enable = cJSON_GetObjectItem(root, "enable");
        if (enable && cJSON_IsBool(enable)) {
            cmd_params.ignition.enable = enable->valueint;
            ret = command_router_execute(COMMAND_IGNITION, &cmd_params);
            ESP_LOGI(TAG, "Ignition command: %s", enable->valueint ? "START" : "STOP");
        } else {
            ESP_LOGE(TAG, "Missing 'enable' field in ignition command");
            ret = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(command, "parachute") == 0) {
        cJSON *deploy = cJSON_GetObjectItem(root, "deploy");
        if (deploy && cJSON_IsBool(deploy)) {
            cmd_params.parachute.deploy = deploy->valueint;
            ret = command_router_execute(COMMAND_DEPLOY_PARACHUTE, &cmd_params);
            ESP_LOGI(TAG, "Parachute command: %s", deploy->valueint ? "DEPLOY" : "ARM");
        } else {
            ESP_LOGE(TAG, "Missing 'deploy' field in parachute command");
            ret = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(command, "explode") == 0) {
        cJSON *armed = cJSON_GetObjectItem(root, "armed");
        cJSON *code = cJSON_GetObjectItem(root, "code");
        if (armed && cJSON_IsBool(armed) && code && cJSON_IsString(code)) {
            cmd_params.explode.armed = armed->valueint;
            strlcpy(cmd_params.explode.code, code->valuestring, sizeof(cmd_params.explode.code));
            ret = command_router_execute(COMMAND_EXPLODE, &cmd_params);
            ESP_LOGI(TAG, "Explode command: armed=%d", armed->valueint);
        } else {
            ESP_LOGE(TAG, "Missing 'armed' or 'code' in explode command");
            ret = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(command, "config") == 0) {
        cJSON *param = cJSON_GetObjectItem(root, "param");
        cJSON *value = cJSON_GetObjectItem(root, "value");
        if (param && cJSON_IsString(param) && value && cJSON_IsString(value)) {
            strlcpy(cmd_params.config_update.param, param->valuestring, sizeof(cmd_params.config_update.param));
            strlcpy(cmd_params.config_update.value, value->valuestring, sizeof(cmd_params.config_update.value));
            ret = command_router_execute(COMMAND_UPDATE_CONFIG, &cmd_params);
            ESP_LOGI(TAG, "Config update: %s = %s", param->valuestring, value->valuestring);
        } else {
            ESP_LOGE(TAG, "Missing 'param' or 'value' in config command");
            ret = ESP_ERR_INVALID_ARG;
        }
        } else if (strcmp(command, "calibrate") == 0) {
        cJSON *sensor_name = cJSON_GetObjectItem(root, "sensor_name");
        cJSON *action = cJSON_GetObjectItem(root, "action");
        cJSON *value = cJSON_GetObjectItem(root, "value");
        if (sensor_name && cJSON_IsString(sensor_name) &&
            action && cJSON_IsString(action)) {
            calibrate_params_t params;
            strlcpy(params.sensor_name, sensor_name->valuestring, sizeof(params.sensor_name));
            strlcpy(params.action, action->valuestring, sizeof(params.action));
            params.value = (value && cJSON_IsNumber(value)) ? (float)value->valuedouble : 0.0f;
            command_param_union_t cmd_params;
            memset(&cmd_params, 0, sizeof(cmd_params));
            memcpy(&cmd_params.calibrate, &params, sizeof(calibrate_params_t));
            ret = command_router_execute(COMMAND_CALIBRATE_SENSOR, &cmd_params);
            ESP_LOGI(TAG, "Calibrate sensor: %s, action=%s, value=%.3f",
                     params.sensor_name, params.action, params.value);
        } else {
            ESP_LOGE(TAG, "Missing 'sensor_name', 'action', or 'value' in calibrate command");
            ret = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(command, "request_data") == 0) {
        cJSON *group = cJSON_GetObjectItem(root, "group");
        cJSON *rate = cJSON_GetObjectItem(root, "rate_hz");
        if (group && cJSON_IsString(group) && rate && cJSON_IsNumber(rate)) {
            strlcpy(cmd_params.telemetry_rate.group, group->valuestring, sizeof(cmd_params.telemetry_rate.group));
            cmd_params.telemetry_rate.rate_hz = (uint32_t)rate->valuedouble;
            ret = command_router_execute(COMMAND_SET_TELEMETRY_RATE, &cmd_params);
            ESP_LOGI(TAG, "Telemetry rate: %s @ %lu Hz", group->valuestring, (unsigned long)cmd_params.telemetry_rate.rate_hz);
        } else {
            ESP_LOGE(TAG, "Missing 'group' or 'rate_hz' in request_data command");
            ret = ESP_ERR_INVALID_ARG;
        }
    } else {
        ESP_LOGW(TAG, "Unknown command: %s", command);
        ret = ESP_ERR_NOT_FOUND;
    }

    cJSON_Delete(root);
    return ret;
}

/* Wrapper for command router – signature matches command_handler_fn_t */
static esp_err_t rocket_cmd_wrapper(void *context, const command_param_union_t *params)
{
    return handle_rocket_command(context, params);
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t rocket_command_service_init(void)
{
    ESP_LOGI(TAG, "Rocket command service initialised");
    return ESP_OK;
}

esp_err_t rocket_command_service_register_handlers(void)
{
    esp_err_t ret = service_register_command(COMMAND_PROCESS_ROCKET_COMMAND, rocket_cmd_wrapper, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register command handler: %d, %s", ret, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Rocket command handler registered");
    return ESP_OK;
}

esp_err_t rocket_command_service_start(void)
{
    /* Build topic: rocket/<mac>/cmd/+ */
    uint8_t mac[6];
    esp_err_t ret = esp_efuse_mac_get_default(mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC address: %d", ret);
        return ret;
    }
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char topic[128];
    snprintf(topic, sizeof(topic), "rocket/%s/cmd/+", mac_str);

    /* Prepare command parameters */
    command_param_union_t cmd_params;
    memset(&cmd_params, 0, sizeof(cmd_params));
    cmd_subscribe_mqtt_params_t *sub = &cmd_params.subscribe_mqtt;
    strlcpy(sub->topic, topic, sizeof(sub->topic));
    sub->qos = 1;

    ret = command_router_execute(COMMAND_SUBSCRIBE_MQTT, &cmd_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to subscribe to %s: %d", topic, ret);
        return ret;
    }
    ESP_LOGI(TAG, "Subscribed to rocket command topics: %s", topic);
    return ESP_OK;
}

esp_err_t rocket_command_service_stop(void)
{
    /* Unsubscribe (optional) */
    ESP_LOGI(TAG, "Rocket command service stopped");
    return ESP_OK;
}