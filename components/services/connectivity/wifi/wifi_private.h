/**
 * @file components/services/communication/wifi_private.h
 * @brief Private definitions for the WiFi Service (internal use only).
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This header defines structures, enumerations, and queue message types used
 * exclusively within the WiFi service implementation. It is not exposed to
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

#ifndef WIFI_PRIVATE_H
#define WIFI_PRIVATE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "wifi_driver_abstraction.h"
#include "command_params.h"
#include "event_types.h"

#include "service_interfaces.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * QUEUE MESSAGE TYPES
 * ============================================================ */

typedef enum {
    QUEUE_MSG_COMMAND,          /**< Incoming command from the command router */
    QUEUE_MSG_DRIVER_EVENT,     /**< Event from the WiFi driver abstraction */
    QUEUE_MSG_RETRY             /**< Timer expiration for reconnect */
} queue_msg_type_t;

/* ============================================================
 * DRIVER EVENT MESSAGE
 * ============================================================ */

typedef struct {
    wifi_driver_event_t event;          /**< Type of driver event */
    union {
        int disconnect_reason;          /**< For DISCONNECTED – reason code */
        esp_netif_ip_info_t ip_info;    /**< For GOT_IP – IP information */
    } data;                             /**< Event-specific data */
} driver_event_msg_t;

/* ============================================================
 * COMMAND MESSAGE
 * ============================================================ */

typedef struct {
    command_type_t cmd_id;         /**< Command identifier */
    union {
        cmd_connect_wifi_params_t connect;      /**< For CMD_CONNECT_WIFI */
        cmd_set_config_wifi_params_t set_config;/**< For CMD_SET_CONFIG_WIFI */
        /* get_status and scan have no data */
    } data;                             /**< Command-specific parameters */
} command_msg_t;

/* ============================================================
 * QUEUE ITEM
 * ============================================================ */

typedef struct {
    queue_msg_type_t type;              /**< Discriminator */
    union {
        command_msg_t cmd;              /**< When type = QUEUE_MSG_COMMAND */
        driver_event_msg_t drv_evt;     /**< When type = QUEUE_MSG_DRIVER_EVENT */
        /* retry has no extra data */
    } msg;                              /**< Message payload */
} queue_item_t;

/* ============================================================
 * WIFI SERVICE STATES
 * ============================================================ */

typedef enum {
    WIFI_STATE_IDLE,            /**< No connection, waiting for command */
    WIFI_STATE_CONNECTING,      /**< Connection attempt in progress */
    WIFI_STATE_CONNECTED,       /**< Successfully connected to AP (may not have IP yet) */
    WIFI_STATE_RECONNECT_WAIT,  /**< Disconnected, waiting before next attempt */
    WIFI_STATE_FAILED           /**< All reconnect attempts exhausted */
} wifi_service_state_t;

/* ============================================================
 * WIFI SERVICE CONFIGURATION
 * ============================================================ */

typedef struct {
    uint32_t min_retry_delay_ms;    /**< Minimum delay between reconnect attempts (ms) */
    uint32_t max_retry_delay_ms;    /**< Maximum delay between reconnect attempts (ms) */
    uint32_t max_retry_attempts;    /**< Max retry attempts after initial failure (0 = infinite) */
} wifi_service_config_t;

/* ============================================================
 * WIFI SERVICE CONTEXT (STATIC)
 * ============================================================ */

struct wifi_service_context {
    TaskHandle_t task;                  /**< Handle of the service task */
    QueueHandle_t queue;                /**< Queue for incoming messages */
    esp_timer_handle_t retry_timer;     /**< Timer for backoff delays */
    wifi_service_state_t state;         /**< Current state of the FSM */
    wifi_service_config_t config;       /**< Active configuration */
    uint32_t retry_count;               /**< Number of consecutive failed attempts */
    uint32_t retry_delay_ms;            /**< Current backoff delay (ms) */
    char current_ssid[32];              /**< SSID of the target AP (copied) */
    char current_password[64];          /**< Password for the target AP (copied) */
    bool user_disconnect;               /**< True if disconnect was commanded by user */
};

#ifdef __cplusplus
}
#endif

#endif /* WIFI_PRIVATE_H */