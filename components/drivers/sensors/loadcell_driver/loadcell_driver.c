#include "loadcell_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "LOADCELL_DRV";

struct loadcell_handle_t {
    gpio_num_t sck_pin;
    gpio_num_t dout_pin;
    loadcell_calibration_t calibration;
    uint32_t filter_window_size;
    float *filter_buffer;
    uint32_t filter_index;
    uint32_t filter_count;
    float filtered_value;
};

static esp_err_t read_hx711_raw(loadcell_handle_t handle, int32_t *raw_count)
{
    for (int retry = 0; retry < 3; retry++) {
        int64_t start = esp_timer_get_time();
        while (gpio_get_level(handle->dout_pin) == 1) {
            if ((esp_timer_get_time() - start) > 200000) break;
            esp_rom_delay_us(10);
        }
        if (gpio_get_level(handle->dout_pin) == 0) {
            int32_t value = 0;
            for (int i = 0; i < 24; i++) {
                gpio_set_level(handle->sck_pin, 1);
                esp_rom_delay_us(1);
                value = (value << 1) | gpio_get_level(handle->dout_pin);
                gpio_set_level(handle->sck_pin, 0);
                esp_rom_delay_us(1);
            }
            gpio_set_level(handle->sck_pin, 1);
            esp_rom_delay_us(1);
            gpio_set_level(handle->sck_pin, 0);
            esp_rom_delay_us(1);
            if (value & (1 << 23)) value |= 0xFF000000;
            *raw_count = value;
            return ESP_OK;
        }
    }
    return ESP_ERR_TIMEOUT;
}

static void update_filter(loadcell_handle_t handle, float new_sample)
{
    if (handle->filter_window_size <= 1) {
        handle->filtered_value = new_sample;
        return;
    }
    handle->filter_buffer[handle->filter_index] = new_sample;
    handle->filter_index = (handle->filter_index + 1) % handle->filter_window_size;
    if (handle->filter_count < handle->filter_window_size) handle->filter_count++;
    float sum = 0;
    for (uint32_t i = 0; i < handle->filter_count; i++) sum += handle->filter_buffer[i];
    handle->filtered_value = sum / handle->filter_count;
}

esp_err_t loadcell_driver_create(const loadcell_config_t *cfg, loadcell_handle_t *out_handle)
{
    if (!cfg || !out_handle) return ESP_ERR_INVALID_ARG;
    loadcell_handle_t handle = calloc(1, sizeof(struct loadcell_handle_t));
    if (!handle) return ESP_ERR_NO_MEM;

    handle->sck_pin = cfg->hw.sck_pin;
    handle->dout_pin = cfg->hw.dout_pin;
    handle->calibration = cfg->calibration;
    handle->filter_window_size = cfg->filter_window_size > 0 ? cfg->filter_window_size : 1;

    if (handle->filter_window_size > 1) {
        handle->filter_buffer = malloc(sizeof(float) * handle->filter_window_size);
        if (!handle->filter_buffer) { free(handle); return ESP_ERR_NO_MEM; }
        memset(handle->filter_buffer, 0, sizeof(float) * handle->filter_window_size);
    }

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
    ESP_LOGI(TAG, "Load cell driver created");
    return ESP_OK;

fail:
    if (handle->filter_buffer) free(handle->filter_buffer);
    free(handle);
    return ret;
}

esp_err_t loadcell_driver_read(loadcell_handle_t handle, float *newtons)
{
    if (!handle || !newtons) return ESP_ERR_INVALID_ARG;
    int32_t raw;
    esp_err_t ret = read_hx711_raw(handle, &raw);
    if (ret != ESP_OK) return ret;
    *newtons = ((float)raw - handle->calibration.offset_raw) * handle->calibration.scale_newtons_per_count;
    return ESP_OK;
}

esp_err_t loadcell_driver_read_filtered(loadcell_handle_t handle, float *newtons)
{
    if (!handle || !newtons) return ESP_ERR_INVALID_ARG;
    float raw_newtons;
    esp_err_t ret = loadcell_driver_read(handle, &raw_newtons);
    if (ret != ESP_OK) return ret;
    update_filter(handle, raw_newtons);
    *newtons = handle->filtered_value;
    return ESP_OK;
}

esp_err_t loadcell_driver_calibrate(loadcell_handle_t handle, float known_newtons)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    int32_t raw;
    esp_err_t ret = read_hx711_raw(handle, &raw);
    if (ret != ESP_OK) return ret;

    if (fabsf(known_newtons) < 0.001f) {
        handle->calibration.offset_raw = raw;
        ESP_LOGI(TAG, "Zero calibration: offset_raw=%ld", raw);
    } else {
        if (handle->calibration.offset_raw == 0) return ESP_ERR_INVALID_STATE;
        int32_t raw_diff = raw - handle->calibration.offset_raw;
        if (raw_diff == 0) return ESP_ERR_INVALID_STATE;
        handle->calibration.scale_newtons_per_count = known_newtons / (float)raw_diff;
        ESP_LOGI(TAG, "Scale calibration: raw_diff=%ld, scale=%.6f N/count", raw_diff, handle->calibration.scale_newtons_per_count);
    }
    return ESP_OK;
}

esp_err_t loadcell_driver_delete(loadcell_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (handle->filter_buffer) free(handle->filter_buffer);
    free(handle);
    return ESP_OK;
}