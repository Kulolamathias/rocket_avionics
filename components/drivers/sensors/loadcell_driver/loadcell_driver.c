/**
 * @file loadcell_driver.c
 * @brief Load Cell Driver – HX711 implementation.
 *
 * =============================================================================
 * IMPLEMENTATION NOTES
 * =============================================================================
 * - HX711 uses bit‑banging with short delays (no FreeRTOS blocking inside reads).
 * - Moving average filter applied in the sampling task.
 * - Sampling task runs at the configured rate (up to 80 Hz) and pushes filtered
 *   values into a queue of depth 1 (overwritten).
 * - Calibration is two‑step: zero then known weight.
 * =============================================================================
 */

#include "loadcell_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "LOADCELL_DRV";

/* Internal handle structure */
struct loadcell_handle_t {
    uint32_t sample_rate_hz;
    uint32_t filter_window_size;
    loadcell_calibration_t calibration;
    gpio_num_t sck_pin;
    gpio_num_t dout_pin;
    /* Filter and sampling state */
    float *filter_buffer;
    uint32_t filter_index;
    uint32_t filter_count;
    float filtered_value;
    volatile bool new_data_ready;
    /* Task and queue for continuous sampling */
    TaskHandle_t sampling_task;
    QueueHandle_t data_queue;
    bool running;
};

/* ============================================================
 * HX711 low‑level functions
 * ============================================================ */

/**
 * @brief Read raw 24‑bit value from HX711 with retries.
 *
 * @param handle   Load cell handle.
 * @param raw_count Pointer to store the raw ADC count.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if DOUT never goes low.
 */
static esp_err_t read_hx711_raw(loadcell_handle_t handle, int32_t *raw_count)
{
    if (!handle || !raw_count) return ESP_ERR_INVALID_ARG;

    /* Retry up to 3 times */
    for (int retry = 0; retry < 3; retry++) {
        /* Wait for DOUT to go low (data ready) – 200 ms timeout */
        int64_t start = esp_timer_get_time();
        while (gpio_get_level(handle->dout_pin) == 1) {
            if ((esp_timer_get_time() - start) > 200000) {
                ESP_LOGW(TAG, "HX711 timeout (retry %d)", retry);
                break;
            }
            esp_rom_delay_us(10);  /* Short delay, not blocking FreeRTOS */
        }

        /* If DOUT is low, read the 24 bits */
        if (gpio_get_level(handle->dout_pin) == 0) {
            int32_t value = 0;
            for (int i = 0; i < 24; i++) {
                gpio_set_level(handle->sck_pin, 1);
                esp_rom_delay_us(1);
                value = (value << 1) | gpio_get_level(handle->dout_pin);
                gpio_set_level(handle->sck_pin, 0);
                esp_rom_delay_us(1);
            }
            /* Extra pulse for gain 128 (channel A) */
            gpio_set_level(handle->sck_pin, 1);
            esp_rom_delay_us(1);
            gpio_set_level(handle->sck_pin, 0);
            esp_rom_delay_us(1);

            /* Sign extend 24‑bit to 32‑bit */
            if (value & (1 << 23)) {
                value |= 0xFF000000;
            }
            *raw_count = value;
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "HX711 still not responding after retries");
    return ESP_ERR_TIMEOUT;
}

/* ============================================================
 * Moving average filter
 * ============================================================ */
static void update_filter(loadcell_handle_t handle, float new_sample)
{
    if (handle->filter_window_size == 1) {
        handle->filtered_value = new_sample;
        return;
    }

    handle->filter_buffer[handle->filter_index] = new_sample;
    handle->filter_index = (handle->filter_index + 1) % handle->filter_window_size;
    if (handle->filter_count < handle->filter_window_size) {
        handle->filter_count++;
    }

    float sum = 0;
    for (uint32_t i = 0; i < handle->filter_count; i++) {
        sum += handle->filter_buffer[i];
    }
    handle->filtered_value = sum / handle->filter_count;
}

/* ============================================================
 * Sampling task (continuous)
 * ============================================================ */
static void sampling_task(void *arg)
{
    loadcell_handle_t handle = (loadcell_handle_t)arg;
    ESP_LOGI(TAG, "=== SAMPLING TASK ENTERED ===");
    uint32_t interval_us = 1000000 / handle->sample_rate_hz;
    int64_t next_time = esp_timer_get_time();

    while (handle->running) {
        ESP_LOGI(TAG, "Sampling loop iteration");
        float newtons;
        esp_err_t ret = loadcell_driver_measure_once(handle, &newtons);
        ESP_LOGI(TAG, "measure_once result: %d, newtons=%.3f", ret, newtons);
        if (ret == ESP_OK) {
            update_filter(handle, newtons);
            handle->new_data_ready = true;
            if (handle->data_queue) {
                float val = handle->filtered_value;
                xQueueOverwrite(handle->data_queue, &val);
            }
        }
        next_time += interval_us;
        int64_t now = esp_timer_get_time();
        if (next_time > now) {
            vTaskDelay(pdMS_TO_TICKS((next_time - now) / 1000));
        } else {
            next_time = now + interval_us;
        }
    }
    vTaskDelete(NULL);
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t loadcell_driver_create(const loadcell_config_t *cfg, loadcell_handle_t *out_handle)
{
    if (!cfg || !out_handle) return ESP_ERR_INVALID_ARG;

    loadcell_handle_t handle = calloc(1, sizeof(struct loadcell_handle_t));
    if (!handle) return ESP_ERR_NO_MEM;

    handle->sample_rate_hz = cfg->sample_rate_hz;
    handle->filter_window_size = cfg->filter_window_size > 0 ? cfg->filter_window_size : 1;
    handle->calibration = cfg->calibration;
    handle->sck_pin = cfg->hw.sck_pin;
    handle->dout_pin = cfg->hw.dout_pin;
    handle->running = false;
    handle->new_data_ready = false;
    handle->filter_count = 0;
    handle->filter_index = 0;
    handle->filtered_value = 0;

    if (handle->filter_window_size > 1) {
        handle->filter_buffer = malloc(sizeof(float) * handle->filter_window_size);
        if (!handle->filter_buffer) {
            free(handle);
            return ESP_ERR_NO_MEM;
        }
        memset(handle->filter_buffer, 0, sizeof(float) * handle->filter_window_size);
    } else {
        handle->filter_buffer = NULL;
    }

    /* Configure GPIOs */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << handle->sck_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) goto fail;
    io_conf.pin_bit_mask = (1ULL << handle->dout_pin);
    io_conf.mode = GPIO_MODE_INPUT;
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) goto fail;
    gpio_set_level(handle->sck_pin, 0);

    *out_handle = handle;
    ESP_LOGI(TAG, "Load cell driver created (rate=%lu Hz, filter=%lu)",
             handle->sample_rate_hz, handle->filter_window_size);
    return ESP_OK;

fail:
    if (handle->filter_buffer) free(handle->filter_buffer);
    free(handle);
    return ret;
}

esp_err_t loadcell_driver_measure_once(loadcell_handle_t handle, float *newtons)
{
    if (!handle || !newtons) return ESP_ERR_INVALID_ARG;

    int32_t raw;
    esp_err_t ret = read_hx711_raw(handle, &raw);
    if (ret != ESP_OK) return ret;

    /* Apply calibration: (raw - offset) * scale */
    float force = ((float)raw - handle->calibration.offset_raw) * handle->calibration.scale_newtons_per_count;
    *newtons = force;
    return ESP_OK;
}

esp_err_t loadcell_driver_start_sampling(loadcell_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (handle->running) return ESP_OK;

    handle->running = true;
    handle->new_data_ready = false;
    /* Create queue for latest data (size 1) */
    handle->data_queue = xQueueCreate(1, sizeof(float));
    if (!handle->data_queue) {
        handle->running = false;
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ret = xTaskCreate(sampling_task, "loadcell_sampling",
                             32768, handle, 15, &handle->sampling_task);  // stack 32KB, priority 15
    if (ret != pdPASS) {
        vQueueDelete(handle->data_queue);
        handle->data_queue = NULL;
        handle->running = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Continuous sampling started");
    return ESP_OK;
}

esp_err_t loadcell_driver_stop_sampling(loadcell_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (!handle->running) return ESP_OK;

    handle->running = false;
    if (handle->sampling_task) {
        vTaskDelete(handle->sampling_task);
        handle->sampling_task = NULL;
    }
    if (handle->data_queue) {
        vQueueDelete(handle->data_queue);
        handle->data_queue = NULL;
    }
    ESP_LOGI(TAG, "Continuous sampling stopped");
    return ESP_OK;
}

esp_err_t loadcell_driver_get_latest(loadcell_handle_t handle, float *newtons)
{
    if (!handle || !newtons) return ESP_ERR_INVALID_ARG;
    if (!handle->running) return ESP_ERR_INVALID_STATE;

    float val;
    if (xQueuePeek(handle->data_queue, &val, 0) == pdTRUE) {
        *newtons = val;
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t loadcell_driver_calibrate(loadcell_handle_t handle, float known_newtons)
{
    vTaskDelay(pdMS_TO_TICKS(250));  /* Short delay to ensure any ongoing sampling is not in the middle of a read */
    if (!handle) return ESP_ERR_INVALID_ARG;

    int32_t raw;
    esp_err_t ret = read_hx711_raw(handle, &raw);
    if (ret != ESP_OK) return ret;

    if (fabsf(known_newtons) < 0.001f) {
        /* Zero calibration */
        handle->calibration.offset_raw = raw;
        ESP_LOGI(TAG, "Zero calibration done: offset_raw=%ld", raw);
    } else {
        /* Scale calibration – requires offset already known */
        if (handle->calibration.offset_raw == 0) {
            ESP_LOGE(TAG, "Must perform zero calibration first");
            return ESP_ERR_INVALID_STATE;
        }
        int32_t raw_diff = raw - handle->calibration.offset_raw;
        if (raw_diff == 0) {
            ESP_LOGE(TAG, "Raw difference zero – cannot calibrate scale");
            return ESP_ERR_INVALID_STATE;
        }
        handle->calibration.scale_newtons_per_count = known_newtons / (float)raw_diff;
        ESP_LOGI(TAG, "Scale calibration done: raw_diff=%ld, scale=%.6f N/count",
                 raw_diff, handle->calibration.scale_newtons_per_count);
    }
    return ESP_OK;
}

esp_err_t loadcell_driver_delete(loadcell_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    loadcell_driver_stop_sampling(handle);
    if (handle->filter_buffer) free(handle->filter_buffer);
    free(handle);
    ESP_LOGI(TAG, "Load cell driver deleted");
    return ESP_OK;
}