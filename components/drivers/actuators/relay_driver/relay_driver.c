#include "relay_driver.h"
#include "esp_log.h"

static const char *TAG = "RELAY";
static gpio_num_t s_pin;
static bool s_active_high;

esp_err_t relay_init(gpio_num_t pin, bool active_high)
{
    s_pin = pin;
    s_active_high = active_high;
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) return ret;
    relay_off();
    ESP_LOGI(TAG, "Relay initialised on GPIO %d (active_high=%d)", pin, active_high);
    return ESP_OK;
}

void relay_on(void)
{
    gpio_set_level(s_pin, s_active_high ? 1 : 0);
    ESP_LOGI(TAG, "Relay ON");
}

void relay_off(void)
{
    gpio_set_level(s_pin, s_active_high ? 0 : 1);
    ESP_LOGI(TAG, "Relay OFF");
}