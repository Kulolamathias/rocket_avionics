/**
 * @file components/core/include/event_types.h
 * @brief System event definitions – pure observations.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Defines all observable facts that can be reported to the core.
 *
 * - Events represent measurements, not decisions.
 * - Each event includes a timestamp and validity flag.
 * - Payload structures are POD (plain old data) and fixed-size.
 *
 * =============================================================================
 * DESIGN PRINCIPLES
 * =============================================================================
 * - No decision logic: events are facts, not interpreted results.
 * - Timestamps are provided by the event producer (service or driver).
 * - Validity flags allow the core to ignore stale/invalid data.
 * - Event IDs are stable and never reused.
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 * - Services post events to event_dispatcher.
 * - event_dispatcher forwards events to state_manager.
 * - state_manager updates system_context and evaluates transitions.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef EVENT_TYPES_H
#define EVENT_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_netif.h"  /* For IP information in WiFi events */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * EVENT IDENTIFIERS
 * ============================================================ */

/**
 * @brief Defines all system event types.
 */
typedef enum
{
    EVENT_NONE = 0,

    /* --------------------------------------------------------
     * System events
     * -------------------------------------------------------- */
    EVENT_SYSTEM_TICK,                  /**< Periodic system tick (time base) */

    /* Timer events */
    EVENT_TIMER_EXPIRED,            /**< One-shot timer expired */
    EVENT_PERIODIC_TIMER_EXPIRED,   /**< Periodic timer expired */

    /* --------------------------------------------------------
     * Kinematics (primary flight signals)
     * -------------------------------------------------------- */
    EVENT_BARO_ALTITUDE_UPDATE,         /**< Raw barometric altitude (m) */
    EVENT_ALTITUDE_UPDATE,              /**< Filtered/derived altitude (m) */
    EVENT_VELOCITY_UPDATE,              /**< Vertical velocity (m/s) */

    /* --------------------------------------------------------
     * IMU data
     * -------------------------------------------------------- */
    EVENT_ACCELERATION_UPDATE,          /**< Linear acceleration (ax, ay, az) in m/s² */
    EVENT_GYRO_UPDATE,                  /**< Angular velocity (gx, gy, gz) in rad/s */
    EVENT_ORIENTATION_UPDATE,           /**< Orientation (roll, pitch, yaw + quaternion) */

    /* --------------------------------------------------------
     * Environmental
     * -------------------------------------------------------- */
    EVENT_PRESSURE_UPDATE,              /**< Atmospheric pressure (Pa) */
    EVENT_TEMPERATURE_UPDATE,           /**< Temperature (°C) */

    /* --------------------------------------------------------
     * Load cell (thrust measurement)
     * -------------------------------------------------------- */
    EVENT_LOADCELL_DATA,          /**< Load cell measurement (force in newtons) */

    EVENT_CAMERA_IMAGE_CAPTURED,

    EVENT_ENGINE_IGNITED,   /**< Engine ignition started (payload: none) */

    /* --------------------------------------------------------
     * WiFi events
     * -------------------------------------------------------- */
    EVENT_WIFI_CONNECTING,          /**< WiFi connection attempt started */
    EVENT_WIFI_CONNECTED,           /**< WiFi connected to AP */
    EVENT_WIFI_GOT_IP,              /**< WiFi obtained IP address */
    EVENT_WIFI_DISCONNECTED,        /**< WiFi disconnected (reason in data) */
    EVENT_WIFI_CONNECTION_FAILED,   /**< WiFi connection failed after retries */
    EVENT_WIFI_STATUS,              /**< WiFi status update (state in data) */

    /* --------------------------------------------------------
     * MQTT events
     * -------------------------------------------------------- */
    EVENT_MQTT_CONNECTING,          /**< MQTT connection attempt started */
    EVENT_MQTT_CONNECTED,           /**< MQTT connected to broker */
    EVENT_MQTT_DISCONNECTED,        /**< MQTT disconnected */
    EVENT_MQTT_CONNECTION_FAILED,   /**< MQTT connection failed after retries */
    EVENT_NETWORK_MESSAGE_RECEIVED, /**< Incoming MQTT message */

    /* --------------------------------------------------------
     * GPS (telemetry only)
     * -------------------------------------------------------- */
    EVENT_GPS_UPDATE,                   /**< Full GPS data (lat, lon, fix, speed) */
    EVENT_GPS_ALTITUDE_UPDATE,          /**< GPS altitude (m) – non-flight-critical */

    // EVENT_GPS_FIX_UPDATED,              /**< New GPS fix available */
    // EVENT_GPS_FIX_LOST,                 /**< GPS fix lost */

    /* --------------------------------------------------------
     * Power monitoring
     * -------------------------------------------------------- */
    EVENT_BATTERY_VOLTAGE_UPDATE,       /**< Battery voltage (V) */
    EVENT_CURRENT_UPDATE,               /**< Current consumption (A) */

    /* Ultrasonic sensor event */
    EVENT_ULTRASONIC_DATA,          /**< Ultrasonic measurement (payload: ultrasonic_data_t) */

    /* --------------------------------------------------------
     * Fault / anomaly events
     * -------------------------------------------------------- */
    EVENT_SENSOR_FAILURE,               /**< Sensor failure with source identification */
    EVENT_TIMEOUT                       /**< Timeout condition (no data / delay breach) */

} event_type_t;

/* ============================================================
 * VALIDITY STRUCTURE
 * ============================================================ */

/**
 * @brief Generic validity flag for all sensor data.
 */
typedef struct
{
    bool valid;                         /**< true if data is valid and up-to-date */
} data_validity_t;

/* ============================================================
 * EVENT PAYLOAD STRUCTURES
 * ============================================================ */

typedef struct {
    float force_newtons;
} loadcell_data_t;

/**
 * @brief Acceleration data (IMU)
 */
typedef struct
{
    float ax_mps2;                      /**< Acceleration along X axis (m/s²) */
    float ay_mps2;                      /**< Acceleration along Y axis (m/s²) */
    float az_mps2;                      /**< Acceleration along Z axis (m/s²) */
    data_validity_t validity;           /**< Validity flag */
} acceleration_data_t;

/**
 * @brief Gyroscope data (IMU)
 */
typedef struct
{
    float gx_rad_s;                     /**< Angular velocity around X axis (rad/s) */
    float gy_rad_s;                     /**< Angular velocity around Y axis (rad/s) */
    float gz_rad_s;                     /**< Angular velocity around Z axis (rad/s) */
    data_validity_t validity;           /**< Validity flag */
} gyro_data_t;

/**
 * @brief Orientation data (Euler + Quaternion)
 *
 * Quaternion is primary for control to avoid gimbal lock.
 */
typedef struct
{
    /* Euler angles (for telemetry/debug) */
    float roll_rad;
    float pitch_rad;
    float yaw_rad;

    /* Quaternion (primary for control) */
    float q_w;
    float q_x;
    float q_y;
    float q_z;

    data_validity_t validity;           /**< Validity flag */
} orientation_data_t;

/**
 * @brief GPS data (telemetry only)
 */
typedef struct
{
    double latitude;                    /**< Latitude (degrees, positive north) */
    double longitude;                   /**< Longitude (degrees, positive east) */
    float altitude_m;                   /**< Altitude above MSL (m) */
    float speed_kmh;                    /**< Ground speed (km/h) */
    uint8_t satellites;                 /**< Number of satellites used */
    bool fix_valid;                     /**< true if fix is usable */
    data_validity_t validity;           /**< Overall validity flag */
} gps_data_t;

/**
 * @brief Sensor failure event payload.
 */
typedef struct
{
    uint8_t sensor_id;                  /**< Identifier of failed sensor */
    uint32_t error_code;                /**< Specific error code */
} sensor_failure_data_t;

/**
 * @brief Altitude update payload.
 */
typedef struct
{
    float value_m;                      /**< Altitude in meters */
    data_validity_t validity;           /**< Validity flag */
} altitude_update_t;

/**
 * @brief Velocity update payload.
 */
typedef struct
{
    float value_mps;                    /**< Velocity in meters per second */
    data_validity_t validity;           /**< Validity flag */
} velocity_update_t;

/**
 * @brief Pressure update payload.
 */
typedef struct
{
    float value_pa;                     /**< Pressure in Pascals */
    data_validity_t validity;           /**< Validity flag */
} pressure_update_t;

/**
 * @brief Temperature update payload.
 */
typedef struct
{
    float value_c;                      /**< Temperature in degrees Celsius */
    data_validity_t validity;           /**< Validity flag */
} temperature_update_t;

/**
 * @brief Battery voltage update payload.
 */
typedef struct
{
    float voltage_v;                    /**< Voltage in Volts */
    data_validity_t validity;           /**< Validity flag */
} battery_voltage_t;

/**
 * @brief Current consumption update payload.
 */
typedef struct
{
    float current_a;                    /**< Current in Amperes */
    data_validity_t validity;           /**< Validity flag */
} current_consumption_t;

/* WiFi event payloads */
typedef struct {
    int reason;                     /**< Disconnection reason code */
} wifi_event_disconnected_t;

typedef struct {
    uint32_t attempts;              /**< Number of attempts made */
} wifi_event_connection_failed_t;

/* MQTT event payloads */
typedef struct {
    int reason;                     /**< Disconnection reason code */
} mqtt_event_disconnected_t;

typedef struct {
    uint32_t attempts;              /**< Number of attempts made */
} mqtt_event_connection_failed_t;

typedef struct {
    char topic[128];                /**< Topic of incoming message */
    uint8_t payload[256];           /**< Message payload */
    size_t payload_len;             /**< Payload length */
    int qos;                        /**< QoS */
    bool retain;                    /**< Retain flag */
} mqtt_message_t;

/**
 * @brief Ultrasonic measurement data.
 */
typedef struct {
    uint32_t distance_cm;           /**< Measured distance in centimeters */
    uint8_t fill_percent;           /**< Calculated fill percentage (0-100) */
    data_validity_t validity;       /**< Validity flag */
} ultrasonic_data_t;

typedef struct {
    char url[128];              /**< URL of captured image */
} camera_image_data_t;


/* ============================================================
 * MAIN EVENT STRUCTURE (TAGGED UNION)
 * ============================================================ */

/**
 * @brief Complete event structure.
 *
 * Contains event type, timestamp, source, and payload.
 */
typedef struct
{
    event_type_t id;                    /**< Event identifier – MUST be first field */
    uint64_t timestamp_us;              /**< Microsecond timestamp (producer time) */
    uint8_t source;                     /**< Event source identifier (optional) */

    /* Payload union – only relevant fields for given id */
    union
    {
        /* Kinematics */
        altitude_update_t altitude;     /**< Altitude update data */
        velocity_update_t velocity;     /**< Velocity update data */

        /* IMU */
        acceleration_data_t acceleration;
        gyro_data_t gyro;
        orientation_data_t orientation;

        /* Environment */
        pressure_update_t pressure;
        temperature_update_t temperature;

        loadcell_data_t loadcell;

        camera_image_data_t camera_image;

        /* GPS */
        gps_data_t gps;
        altitude_update_t gps_altitude;

        /* Power */
        battery_voltage_t battery_voltage;
        current_consumption_t current;

        /* WiFi event payloads */
        wifi_event_disconnected_t wifi_disconnected;
        wifi_event_connection_failed_t wifi_connection_failed;
        esp_netif_ip_info_t ip_info;    /* for EVENT_WIFI_GOT_IP */
        uint32_t status_value;          /* for EVENT_WIFI_STATUS */

        /* MQTT event payloads */
        mqtt_event_disconnected_t mqtt_disconnected;
        mqtt_event_connection_failed_t mqtt_connection_failed;
        mqtt_message_t mqtt_message;

        ultrasonic_data_t ultrasonic;   /**< For EVENT_ULTRASONIC_DATA */
        
        /* Fault */
        sensor_failure_data_t sensor_failure;

        /* Reserved for future expansion */
        uint32_t raw[16];
    } data;                             /**< Event-specific data */

} event_t;

#ifdef __cplusplus
}
#endif

#endif /* EVENT_TYPES_H */