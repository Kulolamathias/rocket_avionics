#if 1



/**
 * @file main.c
 * @brief Test entry – runs all selected tests.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This main file initialises the common infrastructure (core, services,
 * network stack, NVS) once, then runs the specified test suite.
 * New tests can be added by including their headers and calling their run
 * functions in the test suite list.
 *
 * =============================================================================
 * TEST SUITE SELECTION
 * =============================================================================
 * Currently, only timer_service_test is enabled because core_self_test would
 * conflict with real services (it registers its own mock handlers). To run
 * core_self_test, it must be built in a separate configuration without services.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "command_router.h"
#include "event_dispatcher.h"
#include "service_manager.h"

/* Test headers */
// #include "core_self_test.h"   // Disabled – conflicts with real services
// #include "timer_service_test.h"
#include "wifi_mqtt_test.h"
#include "ultrasonic_test.h"


static const char *TAG = "MAIN";

/**
 * @brief List of test run functions.
 * Add new tests here.
 */
static struct {
    const char *name;
    esp_err_t (*run)(void);
} s_tests[] = {
    // { "timer_service", timer_service_test_run },
    // { "wifi_mqtt", wifi_mqtt_test_run },
    // { "ultrasonic", ultrasonic_test_run },
    // { "core_self", core_self_test_run },
};

#define TEST_COUNT (sizeof(s_tests) / sizeof(s_tests[0]))

void app_main(void)
{
    esp_err_t ret;

    /* --------------------------------------------------------------------
     * 1. Core initialisation
     * -------------------------------------------------------------------- */
    command_router_init();
    ret = event_dispatcher_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event_dispatcher_init failed: %d", ret);
        return;
    }

    /* --------------------------------------------------------------------
     * 2. NVS (required for WiFi, etc.)
     * -------------------------------------------------------------------- */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* --------------------------------------------------------------------
     * 3. TCP/IP stack and default event loop
     * -------------------------------------------------------------------- */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* --------------------------------------------------------------------
     * 4. Service manager lifecycle
     * -------------------------------------------------------------------- */
    ret = service_manager_init_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_init_all failed: %d", ret);
        return;
    }

    ret = service_manager_register_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_register_all failed: %d", ret);
        return;
    }

    /* Lock the command router – after this no more handlers can be registered */
    command_router_lock();

    ret = service_manager_start_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_start_all failed: %d", ret);
        return;
    }

    /* --------------------------------------------------------------------
     * 5. Start the event dispatcher (now that everything is ready)
     * -------------------------------------------------------------------- */
    ret = event_dispatcher_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event_dispatcher_start failed: %d", ret);
        return;
    }

    ESP_LOGI(TAG, "System ready. Running tests...");

    /* --------------------------------------------------------------------
     * 6. Run all tests sequentially
     * -------------------------------------------------------------------- */
    for (size_t i = 0; i < TEST_COUNT; i++) {
        ESP_LOGI(TAG, "=== Running test: %s ===", s_tests[i].name);
        ret = s_tests[i].run();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Test %s failed: %d", s_tests[i].name, ret);
        } else {
            ESP_LOGI(TAG, "Test %s passed.", s_tests[i].name);
        }
        /* Optional: small delay between tests */
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "All tests completed. System idling.");

    /* Keep the system alive */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}



































#else 






























#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_camera.h"

// ==================== CONFIGURATION ====================
// ----- Wi-Fi Station (optional) -----
#define WIFI_STA_SSID       "YourWiFiSSID"      // Change to your network SSID
#define WIFI_STA_PASS       "YourWiFiPassword"  // Change to your network password

// ----- Access Point fallback -----
#define WIFI_AP_SSID        "Mathias' Sxx U..."
#define WIFI_AP_PASS        "1234567890223"
#define WIFI_AP_CHANNEL     1
#define WIFI_AP_MAX_CONN    4

// ----- HTTP stream port -----
#define STREAM_PORT         81

// ----- Camera pins (ESP32-S3 common layout) -----
// *** ADJUST THESE FOR YOUR BOARD ***
// Many ESP32-S3 camera modules use these pins (Freenove, Makerfabs, etc.)
#define PWDN_GPIO_NUM       -1
#define RESET_GPIO_NUM      -1
#define XCLK_GPIO_NUM       10
#define SIOD_GPIO_NUM       3   // SDA
#define SIOC_GPIO_NUM       2   // SCL
#define Y9_GPIO_NUM         16
#define Y8_GPIO_NUM         17
#define Y7_GPIO_NUM         18
#define Y6_GPIO_NUM         12
#define Y5_GPIO_NUM         13
#define Y4_GPIO_NUM         14
#define Y3_GPIO_NUM         15
#define Y2_GPIO_NUM         11
#define VSYNC_GPIO_NUM      7
#define HREF_GPIO_NUM       8
#define PCLK_GPIO_NUM       9

// Alternative pin sets (commented) – try if above doesn't work:
// Alternative A (common for Waveshare ESP32-S3 CAM):
// #define PWDN_GPIO_NUM       17
// #define RESET_GPIO_NUM      18
// #define XCLK_GPIO_NUM       0
// #define SIOD_GPIO_NUM       5
// #define SIOC_GPIO_NUM       4
// #define Y9_GPIO_NUM         8
// #define Y8_GPIO_NUM         9
// #define Y7_GPIO_NUM         10
// #define Y6_GPIO_NUM         11
// #define Y5_GPIO_NUM         12
// #define Y4_GPIO_NUM         13
// #define Y3_GPIO_NUM         14
// #define Y2_GPIO_NUM         15
// #define VSYNC_GPIO_NUM      7
// #define HREF_GPIO_NUM       16
// #define PCLK_GPIO_NUM       6

// Alternative B (Unexpected Maker ESP32-S3 CAM):
// #define PWDN_GPIO_NUM       -1
// #define RESET_GPIO_NUM      -1
// #define XCLK_GPIO_NUM       10
// #define SIOD_GPIO_NUM       39
// #define SIOC_GPIO_NUM       40
// #define Y9_GPIO_NUM         48
// #define Y8_GPIO_NUM         11
// #define Y7_GPIO_NUM         12
// #define Y6_GPIO_NUM         14
// #define Y5_GPIO_NUM         16
// #define Y4_GPIO_NUM         18
// #define Y3_GPIO_NUM         17
// #define Y2_GPIO_NUM         15
// #define VSYNC_GPIO_NUM      38
// #define HREF_GPIO_NUM       47
// #define PCLK_GPIO_NUM       13

// ==================== GLOBALS ====================
static const char *TAG = "CAM_STREAM";
static httpd_handle_t server = NULL;
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;

// ==================== Wi-Fi Event Handler ====================
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "STA start, connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGE(TAG, "Disconnected from AP. Reason code: %d", event->reason);
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        // Retry connection
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retrying connection...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ==================== Start Access Point ====================
static void start_access_point(void)
{
    ESP_LOGI(TAG, "Starting Access Point mode...");
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .password = WIFI_AP_PASS,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = WIFI_AP_MAX_CONN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    ESP_ERROR_CHECK(esp_netif_get_ip_info(netif, &ip_info));
    ESP_LOGI(TAG, "AP started. Connect to '%s' password '%s'", WIFI_AP_SSID, WIFI_AP_PASS);
    ESP_LOGI(TAG, "Open http://" IPSTR ":%d/stream in your browser", IP2STR(&ip_info.ip), STREAM_PORT);
}

// ==================== Wi-Fi Station (with fallback) ====================
static void wifi_station_init(void)
{
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_STA_SSID,
            .password = WIFI_STA_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to Wi-Fi...");

    // Wait for connection or failure (30 seconds)
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP successfully!");
    } else {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi. Starting Access Point...");
        start_access_point();
    }
}

// ==================== HTTP Stream Handler ====================
static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char *part_buf = NULL;
    size_t part_buf_len;

    res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    if (res != ESP_OK) return res;

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }

        part_buf_len = snprintf(NULL, 0, "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", fb->len);
        part_buf = malloc(part_buf_len + 1);
        snprintf(part_buf, part_buf_len + 1, "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", fb->len);
        httpd_resp_send_chunk(req, part_buf, part_buf_len);
        httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        httpd_resp_send_chunk(req, "\r\n", 2);
        free(part_buf);
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return res;
}

static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = STREAM_PORT;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t stream_uri = {
            .uri       = "/stream",
            .method    = HTTP_GET,
            .handler   = stream_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &stream_uri);
        ESP_LOGI(TAG, "HTTP server started on port %d", STREAM_PORT);
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
}

// ==================== Camera Initialization ====================
static void init_camera(void)
{
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed (0x%x). Check pin configuration.", err);
        return;
    }
    sensor_t *s = esp_camera_sensor_get();
    ESP_LOGI(TAG, "Camera initialized! Sensor PID: 0x%x", s->id.PID);
}

// ==================== Main ====================
void app_main(void)
{
    // Initialize NVS (needed for Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize network stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize camera
    init_camera();

    // Initialize Wi-Fi (station with AP fallback)
    wifi_station_init();

    // Start web server
    start_webserver();

    // Main loop – nothing else needed
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}






















#endif