/* Thermal warning LED indicator (WS2812 via RMT, GPIO48). */

#pragma once

#include "esp_err.h"

esp_err_t led_thermal_init(void);
void led_thermal_set_warning(bool warning);