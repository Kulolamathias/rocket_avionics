/**
 * @file components/core/include/command_params.h
 * @brief Command parameter structures – deterministic data containers.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Defines the parameter payload structures for commands that require additional
 * data. These structures are allocated on the stack by state_manager during
 * command batch execution, filled by dedicated preparer functions, and passed
 * as void* through command_router to the service handler.
 *
 * =============================================================================
 * DESIGN PRINCIPLES
 * =============================================================================
 * - All structures are POD (plain old data) – no pointers, no constructors.
 * - Fixed size to allow stack allocation and copying.
 * - Each command that requires parameters has a corresponding struct.
 * - The command_param_union_t is sized to accommodate the largest struct.
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 * - state_manager defines a param_preparer function for each command that
 *   needs parameters.
 * - The preparer fills a command_param_union_t buffer.
 * - The buffer pointer is passed to command_router_execute().
 * - The service handler casts the void* back to the correct struct type.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef COMMAND_PARAMS_H
#define COMMAND_PARAMS_H

#include <stdint.h>
#include <stdbool.h>
#include "command_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * COMMAND PARAMETER STRUCTURES
 * ============================================================ */

/**
 * @brief Quaternion orientation (primary for gimbal control).
 */
typedef struct
{
    float q_w;          /**< Quaternion w component */
    float q_x;          /**< Quaternion x component */
    float q_y;          /**< Quaternion y component */
    float q_z;          /**< Quaternion z component */
} gimbal_orientation_params_t;

/**
 * @brief Euler angles (fallback/debug for gimbal control).
 */
typedef struct
{
    float roll_rad;     /**< Roll angle in radians */
    float pitch_rad;    /**< Pitch angle in radians */
    float yaw_rad;      /**< Yaw angle in radians */
} gimbal_angles_params_t;

/**
 * @brief Telemetry configuration.
 */
typedef struct
{
    uint32_t interval_ms;   /**< Transmission interval in milliseconds */
} telemetry_config_params_t;

/**
 * @brief Data logging configuration.
 */
typedef struct
{
    uint32_t sampling_rate_hz;  /**< Sampling rate in Hz */
} logging_config_params_t;

/**
 * @brief Timer start parameters.
 */
typedef struct {
    uint32_t timeout_ms;            /**< Timer duration in milliseconds */
} cmd_start_timer_params_t;

/* ============================================================
 * WiFi command parameters
 * ============================================================ */

/**
 * @brief Parameters for CMD_CONNECT_WIFI.
 */
typedef struct {
    char ssid[32];          /**< SSID of the access point */
    char password[64];      /**< Password (may be empty) */
    uint8_t auth_mode;      /**< Authentication mode (from esp_wifi_types.h) */
} cmd_connect_wifi_params_t;

/**
 * @brief Parameters for CMD_SET_CONFIG_WIFI.
 */
typedef struct {
    uint32_t min_retry_delay_ms;    /**< Minimum delay between reconnect attempts */
    uint32_t max_retry_delay_ms;    /**< Maximum delay between reconnect attempts */
    uint32_t max_retry_attempts;    /**< Max retries (0 = infinite) */
} cmd_set_config_wifi_params_t;

/* ============================================================
 * MQTT command parameters
 * ============================================================ */

/**
 * @brief Parameters for CMD_CONNECT_MQTT.
 */
typedef struct {
    char broker_uri[128];           /**< MQTT broker URI */
    char client_id[64];             /**< Client identifier */
    char username[32];              /**< Username (optional) */
    char password[32];              /**< Password (optional) */
    int keepalive;                  /**< Keepalive interval (seconds) */
    bool disable_clean_session;     /**< If true, retain session state */
    int lwt_qos;                    /**< Last Will QoS */
    bool lwt_retain;                /**< Last Will retain flag */
    char lwt_topic[64];             /**< Last Will topic */
    char lwt_message[64];           /**< Last Will message */
    uint32_t min_retry_delay_ms;    /**< Minimum reconnect delay */
    uint32_t max_retry_delay_ms;    /**< Maximum reconnect delay */
    uint32_t max_retry_attempts;    /**< Max retry attempts (0 = infinite) */
} cmd_connect_mqtt_params_t;

/**
 * @brief Parameters for CMD_SET_CONFIG_MQTT.
 * (Same structure as above; may be reused or a separate one if needed)
 */
typedef cmd_connect_mqtt_params_t cmd_set_config_mqtt_params_t;

/**
 * @brief Parameters for CMD_PUBLISH_MQTT.
 */
typedef struct {
    char topic[128];                /**< Topic to publish to */
    uint8_t payload[512];           /**< Message payload */
    size_t payload_len;             /**< Length of payload */
    int qos;                        /**< QoS (0,1,2) */
    bool retain;                    /**< Retain flag */
} cmd_publish_mqtt_params_t;

/**
 * @brief Parameters for CMD_SUBSCRIBE_MQTT.
 */
typedef struct {
    char topic[128];                /**< Topic to subscribe to */
    int qos;                        /**< Requested QoS */
} cmd_subscribe_mqtt_params_t;

/**
 * @brief Parameters for CMD_UNSUBSCRIBE_MQTT.
 */
typedef struct {
    char topic[128];                /**< Topic to unsubscribe from */
} cmd_unsubscribe_mqtt_params_t;

/**
 * @brief Parameters for COMMAND_LOG_MESSAGE.
 */
typedef struct {
    char message[128];              /**< Message text (null-terminated) */
    uint8_t level;                  /**< Log level: 0=debug,1=info,2=warn,3=error */
} log_message_params_t;


/* ============================================================
 * COMMAND PARAMETER UNION
 * ============================================================ */

/**
 * @brief Union of all command parameter structures.
 *
 * This union provides a single stack buffer large enough for any
 * command parameter set. It is zero-initialized before use.
 */
typedef union
{
    gimbal_orientation_params_t     gimbal_orientation;
    gimbal_angles_params_t          gimbal_angles;
    telemetry_config_params_t       telemetry_config;
    logging_config_params_t         logging_config;
    cmd_start_timer_params_t        timer;
    cmd_connect_wifi_params_t       connect_wifi;
    cmd_set_config_wifi_params_t    set_config_wifi;
    cmd_connect_mqtt_params_t       connect_mqtt;
    cmd_set_config_mqtt_params_t    set_config_mqtt;
    cmd_publish_mqtt_params_t       publish_mqtt;
    cmd_subscribe_mqtt_params_t     subscribe_mqtt;
    cmd_unsubscribe_mqtt_params_t   unsubscribe_mqtt;
    uint32_t                        status_value;      /**< Generic status/value field for simple commands */
    log_message_params_t log_message;   /**< For COMMAND_LOG_MESSAGE */
} command_param_union_t;

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_PARAMS_H */