/*
 * ESP32S3_ECM_V1 配置定义
 * 当前版本：V1.2.2
 * 说明：定义 SoftAP 与 APN 的配置结构、默认值和接口。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_WIFI_DEFAULT_SSID "EC200A"
#define APP_WIFI_DEFAULT_PASS "12345678"
#define APP_WIFI_DEFAULT_CHANNEL 1
#define APP_WIFI_MAX_STA 2

typedef enum {
    APP_APN_PROFILE_MOBILE = 0,
    APP_APN_PROFILE_UNICOM = 1,
    APP_APN_PROFILE_TELECOM = 2,
    APP_APN_PROFILE_CUSTOM = 3,
} app_apn_profile_t;

typedef struct {
    char ssid[33];
    char password[65];
    uint8_t channel;
    app_apn_profile_t apn_profile;
    char custom_apn[32];
} app_config_t;

esp_err_t app_config_nvs_init(void);
void app_config_set_defaults(app_config_t *config);
bool app_config_wifi_channel_is_valid(uint8_t channel);
esp_err_t app_config_validate(const app_config_t *config, const char **error_message);
esp_err_t app_config_load(app_config_t *config);
esp_err_t app_config_save(const app_config_t *config);
const char *app_config_get_apn_profile_label(app_apn_profile_t profile);
const char *app_config_get_effective_apn(const app_config_t *config);
