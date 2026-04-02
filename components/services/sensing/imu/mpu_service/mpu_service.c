/**
 * @file components/services/sensing/imu/mpu_service/mpu_service.c
 * @brief MPU Service – implementation with MQTT publishing.
 */

#include "mpu_service.h"
#include "mpu_driver.h"
#include "service_interfaces.h"
#include "event_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_topic.h"
#include "command_params.h"
#include "cJSON.h"
#include "esp_efuse.h"
#include "esp_mac.h"
#include <string.h>
#include <math.h>

#define DRIVER_CREATION_RETRIES 3


static const char *TAG = "MPU_SVC";

/* IMU MQTT publish rate (Hz) */
#define IMU_PUBLISH_RATE_HZ  20
#define PUBLISH_DIVIDER (MPU_SAMPLE_RATE_HZ / IMU_PUBLISH_RATE_HZ)

/* Ensuring divider is at least 1 */
#if PUBLISH_DIVIDER < 1
#undef PUBLISH_DIVIDER
#define PUBLISH_DIVIDER 1
#endif

/* Moving average window size for published IMU data */
#define IMU_FILTER_WINDOW_SIZE  10

/* Configuration */
#define MPU_I2C_PORT        I2C_NUM_0
#define MPU_I2C_ADDR        0x68
#define MPU_SDA_PIN         8
#define MPU_SCL_PIN         9
#define MPU_SAMPLE_RATE_HZ  200          /* 200 Hz sensor updates */

#define ENABLE_MAGNETOMETER true
#define CALIBRATION_DURATION_MS 5000

static mpu_handle_t s_mpu = NULL;
static TaskHandle_t s_task = NULL;
static bool s_running = false;
static char s_mac_str[13] = {0};   /* MAC address for topic */
static uint32_t s_update_counter = 0;

/* I2C init */
static esp_err_t init_i2c(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = MPU_SDA_PIN,
        .scl_io_num = MPU_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    esp_err_t ret = i2c_param_config(MPU_I2C_PORT, &conf);
    if (ret != ESP_OK) return ret;
    ret = i2c_driver_install(MPU_I2C_PORT, conf.mode, 0, 0, 0);
    return ret;
}

/* Helper: publish IMU data to MQTT */
static void publish_imu_data(const mpu_orientation_t *orient)
{
    if (!orient->orientation_valid) return;

    char topic[128];
    snprintf(topic, sizeof(topic), "rocket/%s/data/imu", s_mac_str);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "timestamp_us", esp_timer_get_time());
    cJSON_AddNumberToObject(root, "roll_deg", orient->roll_rad * 180.0 / M_PI);
    cJSON_AddNumberToObject(root, "pitch_deg", orient->pitch_rad * 180.0 / M_PI);
    cJSON_AddNumberToObject(root, "yaw_deg", orient->yaw_rad * 180.0 / M_PI);
    cJSON_AddNumberToObject(root, "quaternion_w", orient->q_w);
    cJSON_AddNumberToObject(root, "quaternion_x", orient->q_x);
    cJSON_AddNumberToObject(root, "quaternion_y", orient->q_y);
    cJSON_AddNumberToObject(root, "quaternion_z", orient->q_z);

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
}

/* Task: sensor updates and periodic MQTT publish */
static void mpu_service_task(void *arg)
{
    (void)arg;
    uint32_t interval_ms = 1000 / MPU_SAMPLE_RATE_HZ;
    if (interval_ms < 1) interval_ms = 1;
    uint32_t interval_ticks = pdMS_TO_TICKS(interval_ms);

    while (s_running) {
        esp_err_t ret = mpu_driver_update(s_mpu);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "MPU update failed: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        mpu_orientation_t orient;
        mpu_driver_get_orientation(s_mpu, &orient);

        event_t ev_orient = {
            .id = EVENT_ORIENTATION_UPDATE,
            .timestamp_us = esp_timer_get_time(),
            .source = 0,
            .data = { {0} }
        };
        ev_orient.data.orientation.roll_rad = orient.roll_rad;
        ev_orient.data.orientation.pitch_rad = orient.pitch_rad;
        ev_orient.data.orientation.yaw_rad = orient.yaw_rad;
        ev_orient.data.orientation.q_w = orient.q_w;
        ev_orient.data.orientation.q_x = orient.q_x;
        ev_orient.data.orientation.q_y = orient.q_y;
        ev_orient.data.orientation.q_z = orient.q_z;
        ev_orient.data.orientation.validity.valid = orient.orientation_valid;
        service_post_event(&ev_orient);

        s_update_counter++;
        if (s_update_counter >= PUBLISH_DIVIDER) {
            s_update_counter = 0;
            publish_imu_data(&orient);
        }

        vTaskDelay(interval_ticks);
    }
    vTaskDelete(NULL);
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t mpu_service_init(void)
{
    if (s_mpu != NULL) return ESP_ERR_INVALID_STATE;

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_mac_str, sizeof(s_mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    esp_err_t ret = init_i2c();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "I2C master initialised");

        /* MPU driver create with retries */
    mpu_config_t cfg = {
        .i2c_port = MPU_I2C_PORT,
        .i2c_addr = MPU_I2C_ADDR,
        .scl_pin = MPU_SCL_PIN,
        .sda_pin = MPU_SDA_PIN,
        .sample_rate_hz = MPU_SAMPLE_RATE_HZ,
        .enable_magnetometer = ENABLE_MAGNETOMETER,
    };
    memset(&cfg.calibration, 0, sizeof(mpu_calibration_t));

    ret = ESP_FAIL;
    
    for (int attempt = 0; attempt < DRIVER_CREATION_RETRIES; attempt++) {
        ret = mpu_driver_create(&cfg, &s_mpu);
        if (ret == ESP_OK) break;
        ESP_LOGW(TAG, "MPU driver creation attempt %d failed: %d, retrying...", attempt + 1, ret);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MPU driver after 3 attempts");
        return ret;
    }

    ESP_LOGI(TAG, "Starting auto‑calibration (keep sensor still for %d ms)", CALIBRATION_DURATION_MS);
    ret = mpu_driver_calibrate(s_mpu, CALIBRATION_DURATION_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Calibration failed: %d", ret);
    } else {
        ESP_LOGI(TAG, "Calibration done");
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "MPU service initialised (%d Hz, MQTT publish every %d updates)", MPU_SAMPLE_RATE_HZ, PUBLISH_DIVIDER);
    return ESP_OK;
}

esp_err_t mpu_service_register_handlers(void)
{
    return ESP_OK;
}

esp_err_t mpu_service_start(void)
{
    if (s_running) return ESP_ERR_INVALID_STATE;
    s_running = true;
    BaseType_t ret = xTaskCreate(mpu_service_task, "mpu_svc", 8192, NULL, 5, &s_task);
    if (ret != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "MPU service started");
    return ESP_OK;
}