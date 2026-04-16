/**
 * @file components/services/actuation/ignition_relay_service/ignition_relay_service.c
 * @brief Ignition Relay Service – controls relay with auto‑shutdown timer.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - When COMMAND_IGNITION with enable=true is received, the relay is turned on
 *   and a one‑shot timer is started. When the timer expires, the relay is
 *   automatically turned off.
 * - If enable=false is received, the timer is cancelled and the relay is turned off.
 * - If another enable=true is received while the relay is on, the timer is
 *   reset (re‑started) – effectively extending the on‑time.
 * =============================================================================
 */

#include "ignition_relay_service.h"
#include "service_interfaces.h"
#include "command_router.h"
#include "command_params.h"
#include "event_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

static const char *TAG = "IGN_RELAY_SVC";

/* ============================================================
 * Configuration – adjust to your hardware and requirements
 * ============================================================ */
#define IGNITION_RELAY_GPIO   GPIO_NUM_10      /* GPIO pin for relay control */
#define RELAY_ACTIVE_LEVEL    1                /* 1 = high turns on relay, 0 = low turns on */
#define IGNITION_TIMEOUT_MS   2500             /* Auto‑off delay in milliseconds */

/* ============================================================
 * Static variables
 * ============================================================ */
static esp_timer_handle_t s_timer = NULL;      /* One‑shot timer for auto‑shutdown */
static bool s_relay_active = false;            /* Current relay state */

/* ============================================================
 * Timer callback – turns off relay when timer expires
 * ============================================================ */
static void ignition_timeout_callback(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Ignition timeout: turning relay OFF");
    gpio_set_level(IGNITION_RELAY_GPIO, !RELAY_ACTIVE_LEVEL);
    s_relay_active = false;
}

/* ============================================================
 * Helper: start (or restart) the auto‑shutdown timer
 * ============================================================ */
static void start_ignition_timer(void)
{
    if (s_timer == NULL) return;
    /* Stop any existing timer */
    esp_timer_stop(s_timer);
    /* Start a new one‑shot timer */
    esp_err_t ret = esp_timer_start_once(s_timer, IGNITION_TIMEOUT_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ignition timer: %d", ret);
    } else {
        ESP_LOGI(TAG, "Ignition timer started (%d ms)", IGNITION_TIMEOUT_MS);
    }
}

/* ============================================================
 * Helper: stop the timer and turn relay off
 * ============================================================ */
static void stop_ignition_and_timer(void)
{
    if (s_timer) esp_timer_stop(s_timer);
    if (s_relay_active) {
        gpio_set_level(IGNITION_RELAY_GPIO, !RELAY_ACTIVE_LEVEL);
        s_relay_active = false;
        ESP_LOGI(TAG, "Relay turned OFF (manual or timer)");
    }
}

/* ============================================================
 * Command handler for COMMAND_IGNITION
 * ============================================================ */
static esp_err_t handle_ignition(void *context, const command_param_union_t *params)
{
    (void)context;
    if (!params) return ESP_ERR_INVALID_ARG;

    const ignition_params_t *p = &params->ignition;
    ESP_LOGI(TAG, "Ignition command: enable=%d", p->enable);

    if (p->enable) {
        /* Turn relay ON */
        if (!s_relay_active) {
            gpio_set_level(IGNITION_RELAY_GPIO, RELAY_ACTIVE_LEVEL);
            s_relay_active = true;
            ESP_LOGI(TAG, "Relay turned ON");
            /* Post event only when ignition is enabled */
            event_t ev = {
                .id = EVENT_ENGINE_IGNITED,
                .timestamp_us = esp_timer_get_time(),
                .source = 0,
                .data = { {0} }
            };
            service_post_event(&ev);
            ESP_LOGI(TAG, "Posted EVENT_ENGINE_IGNITED");
        } else {
            ESP_LOGI(TAG, "Relay already ON, resetting timer");
        }
        /* Start/reset the auto‑shutdown timer */
        start_ignition_timer();
    } else {
        /* Turn relay OFF and cancel timer */
        stop_ignition_and_timer();
    }

    return ESP_OK;
}

/* Wrapper for command router */
static esp_err_t ignition_wrapper(void *context, const command_param_union_t *params)
{
    return handle_ignition(context, params);
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t ignition_relay_service_init(void)
{
    /* Configure GPIO as output */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << IGNITION_RELAY_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %d", IGNITION_RELAY_GPIO, ret);
        return ret;
    }

    /* Initialise to inactive state */
    gpio_set_level(IGNITION_RELAY_GPIO, !RELAY_ACTIVE_LEVEL);
    s_relay_active = false;

    /* Create the one‑shot timer for auto‑shutdown */
    esp_timer_create_args_t timer_args = {
        .callback = ignition_timeout_callback,
        .arg = NULL,
        .name = "ignition_timer",
        .dispatch_method = ESP_TIMER_TASK,
        .skip_unhandled_events = false,
    };
    ret = esp_timer_create(&timer_args, &s_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ignition timer: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Ignition relay service initialised (GPIO %d, active level=%d, timeout=%d ms)",
             IGNITION_RELAY_GPIO, RELAY_ACTIVE_LEVEL, IGNITION_TIMEOUT_MS);
    return ESP_OK;
}

esp_err_t ignition_relay_service_register_handlers(void)
{
    esp_err_t ret = service_register_command(COMMAND_IGNITION, ignition_wrapper, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ignition command: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "Ignition relay command handlers registered");
    return ESP_OK;
}

esp_err_t ignition_relay_service_start(void)
{
    ESP_LOGI(TAG, "Ignition relay service started");
    return ESP_OK;
}

esp_err_t ignition_relay_service_stop(void)
{
    /* Turn off relay and delete timer */
    if (s_timer) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }
    gpio_set_level(IGNITION_RELAY_GPIO, !RELAY_ACTIVE_LEVEL);
    s_relay_active = false;
    ESP_LOGI(TAG, "Ignition relay service stopped");
    return ESP_OK;
}