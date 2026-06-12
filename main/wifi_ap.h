/*
 * ESP32S3_ECM_V1 SoftAP 接口
 * 当前版本：V1.2.2
 * 说明：提供 SoftAP 初始化、配置应用和 DNS 下发接口。
 */

#pragma once

#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"
#include "esp_netif.h"

esp_err_t wifi_ap_init(void);
esp_netif_t *wifi_ap_get_ap_netif(void);
esp_err_t wifi_ap_apply_config(const app_config_t *config);
esp_err_t wifi_ap_set_dns_server(const esp_ip4_addr_t *dns_addr);
esp_err_t wifi_ap_suspend(void);
esp_err_t wifi_ap_resume(const app_config_t *config);
