/**
 * @file mqtt_service.c
 * @brief MQTT Service – clean implementation.
 */

#include "mqtt_service.h"
#include "mqtt_private.h"
#include "mqtt_client_abstraction.h"
#include "service_interfaces.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "command_params.h"
#include "event_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "mqtt_service";

/* Default configuration values */
#define MQTT_DEFAULT_MIN_RETRY_MS   2000
#define MQTT_DEFAULT_MAX_RETRY_MS   60000
#define MQTT_DEFAULT_MAX_ATTEMPTS   5
#define MQTT_DEFAULT_KEEPALIVE      120

/* Broker configuration – change these to match your setup */
#define CONFIG_MQTT_BROKER_URI  "mqtt://102.223.8.140:1883"
#define CONFIG_MQTT_USERNAME    "mqtt_user"
#define CONFIG_MQTT_PASSWORD    "ega12345"

/* Static service context */
static struct mqtt_service_context s_ctx = {0};

/* Forward declarations */
static void mqtt_service_task(void *pvParameters);
static void process_command(const command_msg_t *cmd);
static void process_client_event(const client_event_msg_t *evt);
static void process_retry(void);
static void set_state(mqtt_service_state_t new_state);
static void emit_event(event_type_t event, void *data);
static void start_reconnect_timer(void);
static void stop_reconnect_timer(void);
static void reconnect_timer_callback(void *arg);
static void client_event_callback(mqtt_client_event_t event, void *data);

/* Command handlers */
static esp_err_t cmd_connect_mqtt_handler(void *context, const command_param_union_t *params);
static esp_err_t cmd_disconnect_mqtt_handler(void *context, const command_param_union_t *params);
static esp_err_t cmd_set_config_mqtt_handler(void *context, const command_param_union_t *params);
static esp_err_t cmd_publish_mqtt_handler(void *context, const command_param_union_t *params);
static esp_err_t cmd_subscribe_mqtt_handler(void *context, const command_param_union_t *params);
static esp_err_t cmd_unsubscribe_mqtt_handler(void *context, const command_param_union_t *params);
static esp_err_t cmd_set_wifi_state_handler(void *context, const command_param_union_t *params);

/* ============================================================
 * PUBLIC API (service manager lifecycle)
 * ============================================================ */

esp_err_t mqtt_service_init(void)
{
    if (s_ctx.queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Create command/event queue */
    s_ctx.queue = xQueueCreate(10, sizeof(queue_item_t));
    if (!s_ctx.queue) {
        return ESP_ERR_NO_MEM;
    }

    /* Create reconnect timer */
    esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_callback,
        .arg = NULL,
        .name = "mqtt_retry"
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_ctx.retry_timer);
    if (err != ESP_OK) {
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return err;
    }

    /* Set default configuration (overridden by commands later) */
    s_ctx.config.min_retry_delay_ms = MQTT_DEFAULT_MIN_RETRY_MS;
    s_ctx.config.max_retry_delay_ms = MQTT_DEFAULT_MAX_RETRY_MS;
    s_ctx.config.max_retry_attempts = MQTT_DEFAULT_MAX_ATTEMPTS;
    s_ctx.config.keepalive = MQTT_DEFAULT_KEEPALIVE;
    /* Set broker credentials */
    strlcpy(s_ctx.config.broker_uri, CONFIG_MQTT_BROKER_URI, sizeof(s_ctx.config.broker_uri));
    strlcpy(s_ctx.config.username, CONFIG_MQTT_USERNAME, sizeof(s_ctx.config.username));
    strlcpy(s_ctx.config.password, CONFIG_MQTT_PASSWORD, sizeof(s_ctx.config.password));
    /* Generate a default client ID based on MAC */
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_ctx.config.client_id, sizeof(s_ctx.config.client_id),
             "rocket_%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Initialize client abstraction with default config (will be reconfigured later) */
    mqtt_client_config_t client_cfg = {
        .broker_uri = s_ctx.config.broker_uri,
        .client_id = s_ctx.config.client_id,
        .username = s_ctx.config.username[0] ? s_ctx.config.username : NULL,
        .password = s_ctx.config.password[0] ? s_ctx.config.password : NULL,
        .keepalive = s_ctx.config.keepalive,
        .disable_clean_session = false,
        .lwt_qos = 0,
        .lwt_retain = false,
        .lwt_topic = NULL,
        .lwt_message = NULL,
        .task_stack_size = 0,
        .task_priority = 0
    };
    err = mqtt_client_init(&client_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init MQTT client: %s", esp_err_to_name(err));
        esp_timer_delete(s_ctx.retry_timer);
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return err;
    }

    /* Register client callback */
    mqtt_client_register_callback(client_event_callback);

    /* Create service task */
    BaseType_t ret = xTaskCreate(mqtt_service_task, "mqtt_svc", 4096, NULL, 5, &s_ctx.task);
    if (ret != pdPASS) {
        mqtt_client_stop();
        esp_timer_delete(s_ctx.retry_timer);
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ctx.state = MQTT_STATE_IDLE;
    s_ctx.wifi_connected = false;
    ESP_LOGI(TAG, "MQTT service initialised (broker: %s)", s_ctx.config.broker_uri);
    return ESP_OK;
}

esp_err_t mqtt_service_register_handlers(void)
{
    esp_err_t ret;
    ret = service_register_command(COMMAND_CONNECT_MQTT, cmd_connect_mqtt_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(COMMAND_DISCONNECT_MQTT, cmd_disconnect_mqtt_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(COMMAND_SET_CONFIG_MQTT, cmd_set_config_mqtt_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(COMMAND_PUBLISH_MQTT, cmd_publish_mqtt_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(COMMAND_SUBSCRIBE_MQTT, cmd_subscribe_mqtt_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(COMMAND_UNSUBSCRIBE_MQTT, cmd_unsubscribe_mqtt_handler, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(COMMAND_MQTT_SET_WIFI_STATE, cmd_set_wifi_state_handler, NULL);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "MQTT command handlers registered");
    return ESP_OK;
}

esp_err_t mqtt_service_start(void)
{
    /* Do not send connect command here; wait for WiFi state update */
    ESP_LOGI(TAG, "MQTT service started (will connect when WiFi is ready)");
    return ESP_OK;
}

/* ============================================================
 * COMMAND HANDLERS
 * ============================================================ */

static esp_err_t cmd_set_wifi_state_handler(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    uint32_t state = params->status_value;
    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = COMMAND_MQTT_SET_WIFI_STATE;
    item.msg.cmd.data.wifi_state = state;
    return (xQueueSend(s_ctx.queue, &item, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t cmd_connect_mqtt_handler(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = COMMAND_CONNECT_MQTT;
    memcpy(&item.msg.cmd.data.connect, &params->connect_mqtt, sizeof(cmd_connect_mqtt_params_t));
    return (xQueueSend(s_ctx.queue, &item, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t cmd_disconnect_mqtt_handler(void *context, const command_param_union_t *params)
{
    (void)context; (void)params;
    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = COMMAND_DISCONNECT_MQTT;
    return (xQueueSend(s_ctx.queue, &item, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t cmd_set_config_mqtt_handler(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = COMMAND_SET_CONFIG_MQTT;
    memcpy(&item.msg.cmd.data.set_config, &params->connect_mqtt, sizeof(cmd_set_config_mqtt_params_t));
    return (xQueueSend(s_ctx.queue, &item, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t cmd_publish_mqtt_handler(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = COMMAND_PUBLISH_MQTT;
    memcpy(&item.msg.cmd.data.publish, &params->publish_mqtt, sizeof(cmd_publish_mqtt_params_t));
    return (xQueueSend(s_ctx.queue, &item, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t cmd_subscribe_mqtt_handler(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = COMMAND_SUBSCRIBE_MQTT;
    memcpy(&item.msg.cmd.data.subscribe, &params->subscribe_mqtt, sizeof(cmd_subscribe_mqtt_params_t));
    return (xQueueSend(s_ctx.queue, &item, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t cmd_unsubscribe_mqtt_handler(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    queue_item_t item;
    item.type = QUEUE_MSG_COMMAND;
    item.msg.cmd.cmd_id = COMMAND_UNSUBSCRIBE_MQTT;
    memcpy(&item.msg.cmd.data.unsubscribe, &params->unsubscribe_mqtt, sizeof(cmd_unsubscribe_mqtt_params_t));
    return (xQueueSend(s_ctx.queue, &item, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

/* ============================================================
 * SERVICE TASK
 * ============================================================ */

static void mqtt_service_task(void *pvParameters)
{
    (void)pvParameters;
    queue_item_t item;
    while (1) {
        if (xQueueReceive(s_ctx.queue, &item, portMAX_DELAY) == pdTRUE) {
            switch (item.type) {
                case QUEUE_MSG_COMMAND:
                    process_command(&item.msg.cmd);
                    break;
                case QUEUE_MSG_CLIENT_EVENT:
                    process_client_event(&item.msg.client_evt);
                    break;
                case QUEUE_MSG_RETRY:
                    process_retry();
                    break;
                default:
                    break;
            }
        }
    }
}

/* ============================================================
 * COMMAND PROCESSING
 * ============================================================ */

static void process_command(const command_msg_t *cmd)
{
    switch (cmd->cmd_id) {
        case COMMAND_CONNECT_MQTT: {
            const cmd_connect_mqtt_params_t *p = &cmd->data.connect;
            /* Update configuration */
            strlcpy(s_ctx.config.broker_uri, p->broker_uri, sizeof(s_ctx.config.broker_uri));
            strlcpy(s_ctx.config.client_id, p->client_id, sizeof(s_ctx.config.client_id));
            strlcpy(s_ctx.config.username, p->username, sizeof(s_ctx.config.username));
            strlcpy(s_ctx.config.password, p->password, sizeof(s_ctx.config.password));
            s_ctx.config.keepalive = p->keepalive;
            s_ctx.config.disable_clean_session = p->disable_clean_session;
            s_ctx.config.lwt_qos = p->lwt_qos;
            s_ctx.config.lwt_retain = p->lwt_retain;
            strlcpy(s_ctx.config.lwt_topic, p->lwt_topic, sizeof(s_ctx.config.lwt_topic));
            strlcpy(s_ctx.config.lwt_message, p->lwt_message, sizeof(s_ctx.config.lwt_message));
            s_ctx.config.min_retry_delay_ms = p->min_retry_delay_ms;
            s_ctx.config.max_retry_delay_ms = p->max_retry_delay_ms;
            s_ctx.config.max_retry_attempts = p->max_retry_attempts;
            /* If WiFi already connected, attempt connection immediately */
            if (s_ctx.wifi_connected && s_ctx.state == MQTT_STATE_IDLE) {
                ESP_LOGI(TAG, "WiFi connected, initiating MQTT connection with broker: %s", s_ctx.config.broker_uri);
                s_ctx.retry_count = 0;
                s_ctx.retry_delay_ms = s_ctx.config.min_retry_delay_ms;
                mqtt_client_config_t client_cfg = {
                    .broker_uri = s_ctx.config.broker_uri,
                    .client_id = s_ctx.config.client_id[0] ? s_ctx.config.client_id : NULL,
                    .username = s_ctx.config.username[0] ? s_ctx.config.username : NULL,
                    .password = s_ctx.config.password[0] ? s_ctx.config.password : NULL,
                    .keepalive = s_ctx.config.keepalive,
                    .disable_clean_session = s_ctx.config.disable_clean_session,
                    .lwt_qos = s_ctx.config.lwt_qos,
                    .lwt_retain = s_ctx.config.lwt_retain,
                    .lwt_topic = s_ctx.config.lwt_topic[0] ? s_ctx.config.lwt_topic : NULL,
                    .lwt_message = s_ctx.config.lwt_message[0] ? s_ctx.config.lwt_message : NULL,
                    .task_stack_size = 0,
                    .task_priority = 0
                };
                mqtt_client_stop();
                esp_err_t err = mqtt_client_init(&client_cfg);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "mqtt_client_init failed: %d", err);
                    set_state(MQTT_STATE_FAILED);
                    break;
                }
                set_state(MQTT_STATE_CONNECTING);
                emit_event(EVENT_MQTT_CONNECTING, NULL);
                err = mqtt_client_start();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "mqtt_client_start failed: %d", err);
                    set_state(MQTT_STATE_FAILED);
                    mqtt_event_connection_failed_t evt = { .attempts = 1 };
                    emit_event(EVENT_MQTT_CONNECTION_FAILED, &evt);
                }
            }
            break;
        }

        case COMMAND_DISCONNECT_MQTT:
            s_ctx.user_disconnect = true;
            if (s_ctx.state == MQTT_STATE_RECONNECT_WAIT) {
                stop_reconnect_timer();
                set_state(MQTT_STATE_IDLE);
            } else if (s_ctx.state == MQTT_STATE_CONNECTING || s_ctx.state == MQTT_STATE_CONNECTED) {
                mqtt_client_stop();
            } else if (s_ctx.state == MQTT_STATE_FAILED) {
                set_state(MQTT_STATE_IDLE);
            }
            break;

        case COMMAND_SET_CONFIG_MQTT: {
            const cmd_connect_mqtt_params_t *p = &cmd->data.set_config;
            strlcpy(s_ctx.config.broker_uri, p->broker_uri, sizeof(s_ctx.config.broker_uri));
            strlcpy(s_ctx.config.client_id, p->client_id, sizeof(s_ctx.config.client_id));
            strlcpy(s_ctx.config.username, p->username, sizeof(s_ctx.config.username));
            strlcpy(s_ctx.config.password, p->password, sizeof(s_ctx.config.password));
            s_ctx.config.keepalive = p->keepalive;
            s_ctx.config.disable_clean_session = p->disable_clean_session;
            s_ctx.config.lwt_qos = p->lwt_qos;
            s_ctx.config.lwt_retain = p->lwt_retain;
            strlcpy(s_ctx.config.lwt_topic, p->lwt_topic, sizeof(s_ctx.config.lwt_topic));
            strlcpy(s_ctx.config.lwt_message, p->lwt_message, sizeof(s_ctx.config.lwt_message));
            s_ctx.config.min_retry_delay_ms = p->min_retry_delay_ms;
            s_ctx.config.max_retry_delay_ms = p->max_retry_delay_ms;
            s_ctx.config.max_retry_attempts = p->max_retry_attempts;
            break;
        }

        case COMMAND_PUBLISH_MQTT:
            if (s_ctx.state != MQTT_STATE_CONNECTED) {
                ESP_LOGW(TAG, "Publish ignored, not connected");
                return;
            }
            {
                const cmd_publish_mqtt_params_t *p = &cmd->data.publish;
                mqtt_client_publish(p->topic, p->payload, p->payload_len, p->qos, p->retain);
            }
            break;

        case COMMAND_SUBSCRIBE_MQTT:
            if (s_ctx.state != MQTT_STATE_CONNECTED) {
                ESP_LOGW(TAG, "Subscribe ignored, not connected");
                return;
            }
            {
                const cmd_subscribe_mqtt_params_t *p = &cmd->data.subscribe;
                mqtt_client_subscribe(p->topic, p->qos);
            }
            break;

        case COMMAND_UNSUBSCRIBE_MQTT:
            if (s_ctx.state != MQTT_STATE_CONNECTED) {
                ESP_LOGW(TAG, "Unsubscribe ignored, not connected");
                return;
            }
            {
                const cmd_unsubscribe_mqtt_params_t *p = &cmd->data.unsubscribe;
                mqtt_client_unsubscribe(p->topic);
            }
            break;

        case COMMAND_MQTT_SET_WIFI_STATE:
            s_ctx.wifi_connected = (cmd->data.wifi_state != 0);
            ESP_LOGI(TAG, "WiFi state updated: %s", s_ctx.wifi_connected ? "connected" : "disconnected");
            if (s_ctx.wifi_connected && s_ctx.state == MQTT_STATE_IDLE) {
                ESP_LOGI(TAG, "WiFi connected, initiating MQTT connection");
                s_ctx.retry_count = 0;
                s_ctx.retry_delay_ms = s_ctx.config.min_retry_delay_ms;
                mqtt_client_config_t client_cfg = {
                    .broker_uri = s_ctx.config.broker_uri,
                    .client_id = s_ctx.config.client_id[0] ? s_ctx.config.client_id : NULL,
                    .username = s_ctx.config.username[0] ? s_ctx.config.username : NULL,
                    .password = s_ctx.config.password[0] ? s_ctx.config.password : NULL,
                    .keepalive = s_ctx.config.keepalive,
                    .disable_clean_session = s_ctx.config.disable_clean_session,
                    .lwt_qos = s_ctx.config.lwt_qos,
                    .lwt_retain = s_ctx.config.lwt_retain,
                    .lwt_topic = s_ctx.config.lwt_topic[0] ? s_ctx.config.lwt_topic : NULL,
                    .lwt_message = s_ctx.config.lwt_message[0] ? s_ctx.config.lwt_message : NULL,
                    .task_stack_size = 0,
                    .task_priority = 0
                };
                mqtt_client_stop();
                esp_err_t err = mqtt_client_init(&client_cfg);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "mqtt_client_init failed: %d", err);
                    set_state(MQTT_STATE_FAILED);
                    break;
                }
                set_state(MQTT_STATE_CONNECTING);
                emit_event(EVENT_MQTT_CONNECTING, NULL);
                err = mqtt_client_start();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "mqtt_client_start failed: %d", err);
                    set_state(MQTT_STATE_FAILED);
                    mqtt_event_connection_failed_t evt = { .attempts = 1 };
                    emit_event(EVENT_MQTT_CONNECTION_FAILED, &evt);
                }
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown command %d", cmd->cmd_id);
            break;
    }
}

/* ============================================================
 * CLIENT EVENT PROCESSING (simplified)
 * ============================================================ */

static void process_client_event(const client_event_msg_t *evt)
{
    switch (evt->event) {
        case MQTT_CLIENT_EVENT_CONNECTED:
            if (s_ctx.state == MQTT_STATE_CONNECTING) {
                set_state(MQTT_STATE_CONNECTED);
                emit_event(EVENT_MQTT_CONNECTED, NULL);
                s_ctx.retry_count = 0;
                s_ctx.retry_delay_ms = s_ctx.config.min_retry_delay_ms;
                ESP_LOGI(TAG, "MQTT connected successfully");
            }
            break;

        case MQTT_CLIENT_EVENT_DISCONNECTED:
            emit_event(EVENT_MQTT_DISCONNECTED, &evt->data.error_code);
            if (s_ctx.user_disconnect) {
                s_ctx.user_disconnect = false;
                set_state(MQTT_STATE_IDLE);
            } else if (s_ctx.wifi_connected) {
                /* Automatic reconnect */
                s_ctx.retry_count++;
                if (s_ctx.config.max_retry_attempts > 0 &&
                    s_ctx.retry_count > s_ctx.config.max_retry_attempts) {
                    set_state(MQTT_STATE_FAILED);
                    mqtt_event_connection_failed_t fail_evt = { .attempts = s_ctx.retry_count };
                    emit_event(EVENT_MQTT_CONNECTION_FAILED, &fail_evt);
                } else {
                    start_reconnect_timer();
                    set_state(MQTT_STATE_RECONNECT_WAIT);
                }
            } else {
                set_state(MQTT_STATE_IDLE);
            }
            break;

        case MQTT_CLIENT_EVENT_DATA:
            emit_event(EVENT_NETWORK_MESSAGE_RECEIVED, (void *)&evt->data.message);
            break;

        default:
            break;
    }
}

/* ============================================================
 * RECONNECT TIMER (simplified)
 * ============================================================ */

static void start_reconnect_timer(void)
{
    uint32_t delay = s_ctx.retry_delay_ms;
    s_ctx.retry_delay_ms *= 2;
    if (s_ctx.retry_delay_ms > s_ctx.config.max_retry_delay_ms)
        s_ctx.retry_delay_ms = s_ctx.config.max_retry_delay_ms;
    esp_timer_stop(s_ctx.retry_timer);
    esp_timer_start_once(s_ctx.retry_timer, delay * 1000);
}

static void stop_reconnect_timer(void)
{
    esp_timer_stop(s_ctx.retry_timer);
}

static void reconnect_timer_callback(void *arg)
{
    (void)arg;
    queue_item_t item = { .type = QUEUE_MSG_RETRY };
    xQueueSend(s_ctx.queue, &item, 0);
}

static void process_retry(void)
{
    if (s_ctx.state == MQTT_STATE_RECONNECT_WAIT && s_ctx.wifi_connected) {
        set_state(MQTT_STATE_CONNECTING);
        emit_event(EVENT_MQTT_CONNECTING, NULL);
        esp_err_t err = mqtt_client_start();
        if (err != ESP_OK) {
            /* Simulate disconnect to retry */
            client_event_msg_t fake_evt;
            fake_evt.event = MQTT_CLIENT_EVENT_DISCONNECTED;
            fake_evt.data.error_code = -1;
            process_client_event(&fake_evt);
        }
    }
}

/* ============================================================
 * CLIENT EVENT CALLBACK
 * ============================================================ */

static void client_event_callback(mqtt_client_event_t event, void *data)
{
    if (!s_ctx.queue) return;
    queue_item_t item;
    item.type = QUEUE_MSG_CLIENT_EVENT;
    item.msg.client_evt.event = event;
    switch (event) {
        case MQTT_CLIENT_EVENT_DISCONNECTED:
            if (data) item.msg.client_evt.data.error_code = *(int *)data;
            break;
        case MQTT_CLIENT_EVENT_DATA:
            if (data) {
                mqtt_client_data_t *src = (mqtt_client_data_t *)data;
                mqtt_message_t *msg = &item.msg.client_evt.data.message;
                size_t copy_len = src->topic_len;
                if (copy_len > sizeof(msg->topic)-1) copy_len = sizeof(msg->topic)-1;
                memcpy(msg->topic, src->topic, copy_len);
                msg->topic[copy_len] = '\0';
                copy_len = src->payload_len;
                if (copy_len > sizeof(msg->payload)) copy_len = sizeof(msg->payload);
                memcpy(msg->payload, src->payload, copy_len);
                msg->payload_len = copy_len;
                msg->qos = src->qos;
                msg->retain = src->retain;
            }
            break;
        case MQTT_CLIENT_EVENT_SUBSCRIBED:
        case MQTT_CLIENT_EVENT_UNSUBSCRIBED:
        case MQTT_CLIENT_EVENT_PUBLISHED:
            if (data) item.msg.client_evt.data.msg_id = *(int *)data;
            break;
        default:
            break;
    }
    xQueueSend(s_ctx.queue, &item, 0);
}

/* ============================================================
 * STATE MANAGEMENT AND EVENT EMISSION
 * ============================================================ */

static void set_state(mqtt_service_state_t new_state)
{
    if (s_ctx.state != new_state) {
        s_ctx.state = new_state;
        ESP_LOGD(TAG, "State -> %d", new_state);
    }
}

static void emit_event(event_type_t event, void *data)
{
    event_t ev = {
        .id = event,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { {0} }
    };
    switch (event) {
        case EVENT_MQTT_DISCONNECTED:
            if (data) ev.data.mqtt_disconnected.reason = *(int *)data;
            break;
        case EVENT_MQTT_CONNECTION_FAILED:
            if (data) ev.data.mqtt_connection_failed.attempts = ((mqtt_event_connection_failed_t*)data)->attempts;
            break;
        case EVENT_NETWORK_MESSAGE_RECEIVED:
            if (data) memcpy(&ev.data.mqtt_message, data, sizeof(mqtt_message_t));
            break;
        default:
            break;
    }
    service_post_event(&ev);
}