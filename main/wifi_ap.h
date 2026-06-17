/* SoftAP control interface. */

#pragma once

#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"
#include "esp_netif.h"

esp_err_t wifi_ap_init(void);
esp_netif_t *wifi_ap_get_ap_netif(void);
esp_err_t wifi_ap_apply_config(const app_config_t *config);
esp_err_t wifi_ap_set_dns_server(const esp_ip4_addr_t *dns_addr);
esp_err_t wifi_ap_set_backup_dns_server(const esp_ip4_addr_t *dns_addr);
