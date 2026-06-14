/* Shared runtime state snapshot. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

typedef struct {
    app_config_t config;
    bool softap_started;
    uint8_t connected_sta_count;
    uint8_t runtime_channel;
    bool thermal_protect_active;
    float internal_temp_celsius;
} app_state_snapshot_t;

void app_state_init(void);
void app_state_set_config(const app_config_t *config);
void app_state_get_config(app_config_t *config);
void app_state_set_softap_started(bool started);
void app_state_set_connected_sta_count(uint8_t connected_sta_count);
void app_state_set_runtime_channel(uint8_t runtime_channel);
void app_state_set_thermal_protect_active(bool active);
void app_state_set_internal_temp_celsius(float internal_temp_celsius);
void app_state_get_snapshot(app_state_snapshot_t *snapshot);
