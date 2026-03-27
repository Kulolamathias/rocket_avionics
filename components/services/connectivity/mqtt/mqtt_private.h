/**
 * @file components/services/communication/mqtt_private.h
 * @brief Private definitions for the MQTT Service (internal use only).
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This header defines structures, enumerations, and queue message types used
 * exclusively within the MQTT service implementation. It is not exposed to
 * other modules.
 *
 * =============================================================================
 * OWNERSHIP
 * =============================================================================
 * - Defines: internal state machine, configuration storage, queue formats.
 * - Does NOT: contain any logic or executable code.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef MQTT_PRIVATE_H
#define MQTT_PRIVATE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "mqtt_client_abstraction.h"
#include "service_interfaces.h"
#include "command_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * QUEUE MESSAGE TYPES
 * ============================================================ */

typedef enum {
    QUEUE_MSG_COMMAND,          /**< Incoming command from the command router */
    QUEUE_MSG_CLIENT_EVENT,     /**< Event from the MQTT client abstraction */
    QUEUE_MSG_RETRY             /**< Timer expiration for reconnect */
} queue_msg_type_t;

/* ============================================================
 * CLIENT EVENT MESSAGE
 * ============================================================ */

typedef struct {
    mqtt_client_event_t event;          /**< Type of client event */
    union {
        int msg_id;                     /**< For SUBSCRIBED, UNSUBSCRIBED, PUBLISHED */
        int error_code;                 /**< For DISCONNECTED */
        mqtt_message_t message;         /**< For DATA – contains copied topic and payload */
    } data;                             /**< Event-specific data */
} client_event_msg_t;

/* ============================================================
 * COMMAND MESSAGE
 * ============================================================ */

typedef struct {
    command_type_t cmd_id;         /**< Command identifier */
    union {
        cmd_connect_mqtt_params_t connect;      /**< For CMD_CONNECT_MQTT */
        cmd_set_config_mqtt_params_t set_config;/**< For CMD_SET_CONFIG_MQTT */
        cmd_publish_mqtt_params_t publish;      /**< For CMD_PUBLISH_MQTT */
        cmd_subscribe_mqtt_params_t subscribe;  /**< For CMD_SUBSCRIBE_MQTT */
        cmd_unsubscribe_mqtt_params_t unsubscribe; /**< For CMD_UNSUBSCRIBE_MQTT */
        uint32_t wifi_state;                    /**< For CMD_MQTT_SET_WIFI_STATE */
    } data;                             /**< Command-specific parameters */
} command_msg_t;

/* ============================================================
 * QUEUE ITEM
 * ============================================================ */

typedef struct {
    queue_msg_type_t type;              /**< Discriminator */
    union {
        command_msg_t cmd;              /**< When type = QUEUE_MSG_COMMAND */
        client_event_msg_t client_evt;  /**< When type = QUEUE_MSG_CLIENT_EVENT */
        /* retry has no extra data */
    } msg;                              /**< Message payload */
} queue_item_t;

/* ============================================================
 * MQTT SERVICE STATES
 * ============================================================ */

typedef enum {
    MQTT_STATE_IDLE,            /**< No connection, waiting for command */
    MQTT_STATE_CONNECTING,      /**< Connection attempt in progress */
    MQTT_STATE_CONNECTED,       /**< Successfully connected to broker */
    MQTT_STATE_RECONNECT_WAIT,  /**< Disconnected, waiting before next attempt */
    MQTT_STATE_FAILED           /**< All reconnect attempts exhausted */
} mqtt_service_state_t;

/* ============================================================
 * MQTT SERVICE CONFIGURATION
 * ============================================================ */

typedef struct {
    char broker_uri[128];               /**< Broker URI (copied) */
    char client_id[64];                 /**< Client identifier */
    char username[32];                  /**< Username (if any) */
    char password[32];                  /**< Password (if any) */
    int keepalive;                      /**< Keepalive interval (seconds) */
    bool disable_clean_session;         /**< true = retain session state */
    int lwt_qos;                        /**< Last Will QoS */
    bool lwt_retain;                    /**< Last Will retain flag */
    char lwt_topic[64];                 /**< Last Will topic */
    char lwt_message[64];               /**< Last Will message */
    uint32_t min_retry_delay_ms;        /**< Minimum delay between reconnect attempts (ms) */
    uint32_t max_retry_delay_ms;        /**< Maximum delay between reconnect attempts (ms) */
    uint32_t max_retry_attempts;        /**< Max retry attempts after initial failure (0 = infinite) */
} mqtt_service_config_t;

/* ============================================================
 * MQTT SERVICE CONTEXT (STATIC)
 * ============================================================ */

struct mqtt_service_context {
    TaskHandle_t task;                  /**< Handle of the service task */
    QueueHandle_t queue;                /**< Queue for incoming messages */
    esp_timer_handle_t retry_timer;     /**< Timer for backoff delays */
    mqtt_service_state_t state;         /**< Current state of the FSM */
    mqtt_service_config_t config;       /**< Active configuration */
    uint32_t retry_count;               /**< Number of consecutive failed attempts */
    uint32_t retry_delay_ms;            /**< Current backoff delay (ms) */
    bool user_disconnect;               /**< True if disconnect was commanded by user */
    bool wifi_connected;                /**< Track WiFi state to pause/resume reconnection */
};

#ifdef __cplusplus
}
#endif

#endif /* MQTT_PRIVATE_H */