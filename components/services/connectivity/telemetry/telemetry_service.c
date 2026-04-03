#include "telemetry_service.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "event_types.h"
#include "system_context.h"
#include "state_manager.h"        // for state_manager_get_context()
#include "esp_log.h"
#include "mqtt_topic.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "esp_err.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "TELEMETRY_SVC";

static char s_mac_str[13] = {0};

/* Command handler for telemetry transmission */
static esp_err_t cmd_send_telemetry(void *context, const command_param_union_t *params)
{
    (void)context;
    (void)params;

    /* Get latest orientation from system context */
    const system_context_t *ctx = state_manager_get_context();
    if (!ctx->orientation_valid) return ESP_OK;

    char topic[128];
    snprintf(topic, sizeof(topic), "rocket/%s/data/imu", s_mac_str);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "timestamp_us", esp_timer_get_time());
    cJSON_AddNumberToObject(root, "roll_deg", ctx->orientation.roll_rad * 180.0 / M_PI);
    cJSON_AddNumberToObject(root, "pitch_deg", ctx->orientation.pitch_rad * 180.0 / M_PI);
    cJSON_AddNumberToObject(root, "yaw_deg", ctx->orientation.yaw_rad * 180.0 / M_PI);
    cJSON_AddNumberToObject(root, "quaternion_w", ctx->orientation.q_w);
    cJSON_AddNumberToObject(root, "quaternion_x", ctx->orientation.q_x);
    cJSON_AddNumberToObject(root, "quaternion_y", ctx->orientation.q_y);
    cJSON_AddNumberToObject(root, "quaternion_z", ctx->orientation.q_z);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        cmd_publish_mqtt_params_t pub_params = {
            .topic = "",
            .payload = "",
            .payload_len = 0,
            .qos = 1,
            .retain = false
        };
        strlcpy(pub_params.topic, topic, sizeof(pub_params.topic));
        strlcpy((char*)pub_params.payload, json_str, sizeof(pub_params.payload));
        pub_params.payload_len = strlen(json_str);

        command_param_union_t cmd_params;
        memcpy(&cmd_params.publish_mqtt, &pub_params, sizeof(cmd_publish_mqtt_params_t));
        command_router_execute(COMMAND_PUBLISH_MQTT, &cmd_params);
        free(json_str);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

/* Wrapper for command router – matches command_handler_fn_t signature */
static esp_err_t cmd_send_telemetry_wrapper(void *context, const command_param_union_t *params)
{
    return cmd_send_telemetry(context, params);
}

/* Public API */
esp_err_t telemetry_service_init(void)
{
    /* Get MAC address for topic */
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_mac_str, sizeof(s_mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Telemetry service initialised (MAC: %s)", s_mac_str);
    return ESP_OK;
}

esp_err_t telemetry_service_register_handlers(void)
{
    return service_register_command(COMMAND_SEND_TELEMETRY, cmd_send_telemetry_wrapper, NULL);
}

esp_err_t telemetry_service_start(void)
{
    ESP_LOGI(TAG, "Telemetry service started");
    return ESP_OK;
}