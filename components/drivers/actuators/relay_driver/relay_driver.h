#ifndef RELAY_DRIVER_H
#define RELAY_DRIVER_H

#include "esp_err.h"
#include "driver/gpio.h"

esp_err_t relay_init(gpio_num_t pin, bool active_high);
void relay_on(void);
void relay_off(void);

#endif