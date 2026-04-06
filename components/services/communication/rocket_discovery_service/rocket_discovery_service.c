/**
 * @file components/services/communication/rocket_discovery_service/rocket_discovery_service.c
 * @brief Rocket Discovery Service – implementation.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Listens for EVENT_MQTT_CONNECTED (or a combination of WiFi and MQTT ready).
 * - Publishes discovery message once on connection, then periodically (every 30 s).
 * - Uses the existing MQTT publish command.
 * =============================================================================
 * 
 * @author matthithyahu
 * @date 2026-04-06
 */

#include "rocket_discovery_service.h"
#include "service_interfaces.h"
#include "command_router.h"
#include "command_params.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_efuse.h"
#include "esp_mac.h"
#include <string.h>
#include <inttypes.h>

static const char *TAG = "RKT_DISCOVERY";

#define DISCOVERY_TOPIC "rocket/discovery"
#define PUBLISH_INTERVAL_MS 5000   /* 5 seconds */

static esp_timer_handle_t s_timer = NULL;

/* Capabilities list */
static const char *s_capabilities[] = {
    "ignition", "parachute", "explode", "config", "calibrate", "request_data"
};
#define CAP_COUNT (sizeof(s_capabilities) / sizeof(s_capabilities[0]))

/* Build JSON payload */
static void build_discovery_payload(char *buffer, size_t buflen)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char caps_buf[256] = "";
    for (int i = 0; i < CAP_COUNT; i++) {
        if (i > 0) strlcat(caps_buf, ",", sizeof(caps_buf));
        strlcat(caps_buf, "\"", sizeof(caps_buf));
        strlcat(caps_buf, s_capabilities[i], sizeof(caps_buf));
        strlcat(caps_buf, "\"", sizeof(caps_buf));
    }

    snprintf(buffer, buflen,
             "{"
             "\"mac\":\"%s\","
             "\"device_type\":\"engine_controller\","
             "\"status\":\"online\","
             "\"capabilities\":[%s],"
             "\"timestamp\":%" PRIu64
             "}",
             mac_str, caps_buf, (uint64_t)(esp_timer_get_time() / 1000000));
}

/* Publish discovery message */
static void publish_discovery(void)
{
    char payload[512];
    build_discovery_payload(payload, sizeof(payload));

    command_param_union_t cmd_params;
    memset(&cmd_params, 0, sizeof(cmd_params));
    cmd_publish_mqtt_params_t *pub = &cmd_params.publish_mqtt;
    strlcpy(pub->topic, DISCOVERY_TOPIC, sizeof(pub->topic));
    strlcpy((char*)pub->payload, payload, sizeof(pub->payload));
    pub->payload_len = strlen(payload);
    pub->qos = 1;
    pub->retain = false;

    esp_err_t ret = command_router_execute(COMMAND_PUBLISH_MQTT, &cmd_params);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Discovery published: %s", payload);
    } else {
        ESP_LOGW(TAG, "Failed to publish discovery: %d", ret);
    }
}

/* Timer callback */
static void timer_callback(void *arg)
{
    (void)arg;
    publish_discovery();
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t rocket_discovery_service_init(void)
{
    esp_timer_create_args_t timer_args = {
        .callback = timer_callback,
        .arg = NULL,
        .name = "discovery_timer"
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create discovery timer: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "Rocket discovery service initialised");
    return ESP_OK;
}

esp_err_t rocket_discovery_service_register_handlers(void)
{
    /* No command handlers needed */
    return ESP_OK;
}

esp_err_t rocket_discovery_service_start(void)
{
    /* Publish immediately */
    publish_discovery();
    /* Then start periodic timer */
    esp_timer_start_periodic(s_timer, PUBLISH_INTERVAL_MS * 1000);
    ESP_LOGI(TAG, "Rocket discovery service started");
    return ESP_OK;
}

esp_err_t rocket_discovery_service_stop(void)
{
    if (s_timer) esp_timer_stop(s_timer);
    ESP_LOGI(TAG, "Rocket discovery service stopped");
    return ESP_OK;
}