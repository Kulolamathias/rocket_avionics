/**
 * @file loadcell_service.c
 * @brief Load Cell Service – high‑frequency thrust measurement.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - Runs a dedicated task that reads the load cell at a configurable rate.
 * - Applies moving average filter.
 * - Publishes full engine telemetry to rocket/<mac>/data/engine.
 * - Automatically calibrates zero (assumes no load at startup) and uses
 *   a pre‑determined scale factor (from calibration with 228g weight).
 * - Starts sampling immediately when the service starts.
 * =============================================================================
 */

#include "loadcell_service.h"
#include "loadcell_driver.h"
#include "service_interfaces.h"
#include "command_params.h"
#include "event_types.h"
#include "esp_log.h"
#include "esp_efuse.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "LOADCELL_SVC";

/* ============================================================
 * CONFIGURATION
 * ============================================================ */
#define LOADCELL_SAMPLE_RATE_HZ     80
#define LOADCELL_PUBLISH_RATE_HZ    10
#define LOADCELL_FILTER_WINDOW      10

/* Hardware pins */
#define LOADCELL_SCK_PIN            5
#define LOADCELL_DOUT_PIN           4

/* Pre‑determined scale factor (from calibration with 228g = 2.2367 N) */
#define LOADCELL_SCALE_N_PER_COUNT  0.000095f

/* Placeholder values for other engine parameters */
#define DEFAULT_MASS_FLOW_RATE_KGPS  2.5f
#define DEFAULT_SPECIFIC_IMPULSE_S   280.0f
#define DEFAULT_TOTAL_IMPULSE_NS     150000.0f
#define DEFAULT_BURN_RATE_MMS        5.2f
#define DEFAULT_BURN_TIME_REMAINING_S 3.5f

/* ============================================================
 * Internal state
 * ============================================================ */
typedef struct {
    loadcell_handle_t driver;
    TaskHandle_t task;
    bool running;
    float last_filtered;
    char mac_str[13];
} loadcell_svc_ctx_t;

static loadcell_svc_ctx_t s_ctx = {0};

/* ============================================================
 * Helper: publish full engine telemetry
 * ============================================================ */
static void publish_engine_telemetry(float thrust_n)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "rocket/%s/data/engine", s_ctx.mac_str);

    char json[256];
    snprintf(json, sizeof(json),
             "{"
             "\"timestamp_us\":%lld,"
             "\"thrust_n\":%.3f,"
             "\"mass_flow_rate_kgps\":%.1f,"
             "\"specific_impulse_s\":%.1f,"
             "\"total_impulse_ns\":%.1f,"
             "\"burn_rate_mms\":%.1f,"
             "\"burn_time_remaining_s\":%.1f"
             "}",
             esp_timer_get_time(),
             thrust_n,
             DEFAULT_MASS_FLOW_RATE_KGPS,
             DEFAULT_SPECIFIC_IMPULSE_S,
             DEFAULT_TOTAL_IMPULSE_NS,
             DEFAULT_BURN_RATE_MMS,
             DEFAULT_BURN_TIME_REMAINING_S);

    command_param_union_t params;
    memset(&params, 0, sizeof(params));
    strlcpy(params.publish_mqtt.topic, topic, sizeof(params.publish_mqtt.topic));
    strlcpy((char*)params.publish_mqtt.payload, json, sizeof(params.publish_mqtt.payload));
    params.publish_mqtt.payload_len = strlen(json);
    params.publish_mqtt.qos = 1;
    params.publish_mqtt.retain = false;

    command_router_execute(COMMAND_PUBLISH_MQTT, &params);
    ESP_LOGD(TAG, "Published engine telemetry: thrust=%.3f N", thrust_n);
}

/* ============================================================
 * Sampling task
 * ============================================================ */
static void sampling_task(void *arg)
{
    (void)arg;
    uint32_t interval_us = 1000000 / LOADCELL_SAMPLE_RATE_HZ;
    int64_t next_time = esp_timer_get_time();
    uint32_t publish_interval_us = 1000000 / LOADCELL_PUBLISH_RATE_HZ;
    int64_t next_publish = next_time;

    while (s_ctx.running) {
        float force;
        esp_err_t ret = loadcell_driver_read_filtered(s_ctx.driver, &force);
        if (ret == ESP_OK) {
            s_ctx.last_filtered = force;
            /* Post event to core */
            event_t ev = {
                .id = EVENT_LOADCELL_DATA,
                .timestamp_us = esp_timer_get_time(),
                .source = 0,
                .data = { {0} }
            };
            ev.data.loadcell.force_newtons = force;
            service_post_event(&ev);
        } else {
            ESP_LOGW(TAG, "Read failed: %d", ret);
        }

        int64_t now = esp_timer_get_time();
        if (now >= next_publish && s_ctx.last_filtered != 0) {
            publish_engine_telemetry(s_ctx.last_filtered);
            next_publish = now + publish_interval_us;
        }

        next_time += interval_us;
        now = esp_timer_get_time();
        if (next_time > now) {
            vTaskDelay(pdMS_TO_TICKS((next_time - now) / 1000));
        } else {
            next_time = now + interval_us;
        }
    }
    vTaskDelete(NULL);
}

/* ============================================================
 * Command handlers
 * ============================================================ */
static esp_err_t cmd_loadcell_start_sampling(void *context, const command_param_union_t *params)
{
    (void)context; (void)params;
    if (!s_ctx.driver) return ESP_ERR_INVALID_STATE;
    if (s_ctx.running) return ESP_OK;

    s_ctx.running = true;
    BaseType_t ret = xTaskCreate(sampling_task, "loadcell_sampling", 8192, NULL, 5, &s_ctx.task);
    if (ret != pdPASS) {
        s_ctx.running = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Sampling started (%d Hz)", LOADCELL_SAMPLE_RATE_HZ);
    return ESP_OK;
}

static esp_err_t cmd_loadcell_stop_sampling(void *context, const command_param_union_t *params)
{
    (void)context; (void)params;
    if (!s_ctx.running) return ESP_OK;
    s_ctx.running = false;
    if (s_ctx.task) {
        vTaskDelete(s_ctx.task);
        s_ctx.task = NULL;
    }
    ESP_LOGI(TAG, "Sampling stopped");
    return ESP_OK;
}

static esp_err_t cmd_loadcell_calibrate_zero(void *context, const command_param_union_t *params)
{
    (void)context; (void)params;
    if (!s_ctx.driver) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = loadcell_driver_calibrate(s_ctx.driver, 0.0f);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Zero calibration done");
    } else {
        ESP_LOGE(TAG, "Zero calibration failed: %d", ret);
    }
    return ret;
}

static esp_err_t cmd_loadcell_calibrate_scale(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    if (!s_ctx.driver) return ESP_ERR_INVALID_STATE;
    float known_newtons = params->status_value;
    esp_err_t ret = loadcell_driver_calibrate(s_ctx.driver, known_newtons);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Scale calibration done with %.3f N", known_newtons);
    } else {
        ESP_LOGE(TAG, "Scale calibration failed: %d", ret);
    }
    return ret;
}

static esp_err_t cmd_loadcell_get_last(void *context, const command_param_union_t *params)
{
    (void)context; (void)params;
    if (!s_ctx.driver) return ESP_ERR_INVALID_STATE;
    event_t ev = {
        .id = EVENT_LOADCELL_DATA,
        .timestamp_us = esp_timer_get_time(),
        .source = 0,
        .data = { {0} }
    };
    ev.data.loadcell.force_newtons = s_ctx.last_filtered;
    service_post_event(&ev);
    return ESP_OK;
}

static esp_err_t cmd_calibrate_sensor(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;
    const calibrate_params_t *p = &params->calibrate;
    if (strcmp(p->sensor_name, "loadcell") != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (strcmp(p->action, "zero") == 0) {
        return loadcell_driver_calibrate(s_ctx.driver, 0.0f);
    } else if (strcmp(p->action, "scale") == 0) {
        float force_n = p->value * 9.81f;
        return loadcell_driver_calibrate(s_ctx.driver, force_n);
    }
    return ESP_ERR_INVALID_ARG;
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t loadcell_service_init(void)
{
    if (s_ctx.driver != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Get MAC address for topic */
    uint8_t mac[6];
    esp_err_t ret = esp_efuse_mac_get_default(mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC: %d", ret);
        return ret;
    }
    snprintf(s_ctx.mac_str, sizeof(s_ctx.mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Create driver with initial scale (will be updated by auto‑calibration) */
    loadcell_config_t cfg = {
        .sample_rate_hz = LOADCELL_SAMPLE_RATE_HZ,
        .filter_window_size = LOADCELL_FILTER_WINDOW,
        .calibration = { .offset_raw = 0, .scale_newtons_per_count = 1.0f },
        .hw = { .sck_pin = LOADCELL_SCK_PIN, .dout_pin = LOADCELL_DOUT_PIN }
    };
    ret = loadcell_driver_create(&cfg, &s_ctx.driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create driver: %d", ret);
        return ret;
    }

    s_ctx.running = false;
    s_ctx.task = NULL;
    s_ctx.last_filtered = 0;
    ESP_LOGI(TAG, "Load cell service initialised (sample=%d Hz, publish=%d Hz, filter=%d)",
             LOADCELL_SAMPLE_RATE_HZ, LOADCELL_PUBLISH_RATE_HZ, LOADCELL_FILTER_WINDOW);
    return ESP_OK;
}

esp_err_t loadcell_service_register_handlers(void)
{
    esp_err_t ret;
    ret = service_register_command(COMMAND_LOADCELL_START_SAMPLING, cmd_loadcell_start_sampling, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(COMMAND_LOADCELL_STOP_SAMPLING, cmd_loadcell_stop_sampling, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(COMMAND_LOADCELL_CALIBRATE_ZERO, cmd_loadcell_calibrate_zero, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(COMMAND_LOADCELL_CALIBRATE_SCALE, cmd_loadcell_calibrate_scale, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(COMMAND_LOADCELL_GET_LAST, cmd_loadcell_get_last, NULL);
    if (ret != ESP_OK) return ret;
    ret = service_register_command(COMMAND_CALIBRATE_SENSOR, cmd_calibrate_sensor, NULL);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Load cell command handlers registered");
    return ESP_OK;
}

esp_err_t loadcell_service_start(void)
{
    /* Auto‑calibrate zero (assume no load) */
    ESP_LOGI(TAG, "Auto‑calibrating zero (keep sensor unloaded)...");
    esp_err_t ret = loadcell_driver_calibrate(s_ctx.driver, 0.0f);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Auto zero calibration failed: %d", ret);
        return ret;
    }
    /* Set the pre‑determined scale factor */
    ret = loadcell_driver_set_scale(s_ctx.driver, LOADCELL_SCALE_N_PER_COUNT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set scale factor: %d", ret);
        return ret;
    }

    /* Start sampling automatically */
    ret = cmd_loadcell_start_sampling(NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sampling: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "Load cell service started and sampling");
    vTaskDelay(pdMS_TO_TICKS(200)); // let HX711 settle
    return ESP_OK;
}

esp_err_t loadcell_service_stop(void)
{
    if (s_ctx.running) {
        cmd_loadcell_stop_sampling(NULL, NULL);
    }
    if (s_ctx.driver) {
        loadcell_driver_delete(s_ctx.driver);
        s_ctx.driver = NULL;
    }
    ESP_LOGI(TAG, "Load cell service stopped");
    return ESP_OK;
}