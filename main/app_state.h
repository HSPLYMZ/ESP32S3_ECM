/*
 * ESP32S3_ECM_V1 运行状态定义
 * 当前版本：V1.2
 * 说明：保存最小路由闭环所需的共享运行状态。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

typedef struct {
    app_config_t config;
    bool softap_started;
    uint8_t connected_sta_count;
    uint8_t runtime_channel;
    bool power_sleeping;
    bool thermal_protect_active;
    float internal_temp_celsius;
} app_state_snapshot_t;

void app_state_init(void);
void app_state_set_config(const app_config_t *config);
void app_state_get_config(app_config_t *config);
void app_state_set_softap_started(bool started);
void app_state_set_connected_sta_count(uint8_t connected_sta_count);
void app_state_set_runtime_channel(uint8_t runtime_channel);
void app_state_set_power_sleeping(bool sleeping);
void app_state_set_thermal_protect_active(bool active);
void app_state_set_internal_temp_celsius(float internal_temp_celsius);
void app_state_get_snapshot(app_state_snapshot_t *snapshot);
