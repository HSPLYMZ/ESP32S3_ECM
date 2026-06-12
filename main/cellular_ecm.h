/* EC200A ECM uplink interface. */

#pragma once

#include <stdbool.h>

#include "app_config.h"
#include "esp_err.h"
#include "esp_netif.h"

typedef enum {
    CELLULAR_UPLINK_MODE_ECM = 0,
} cellular_uplink_mode_t;

typedef struct {
    bool usb_connected;
    bool at_ready;
    bool uplink_connected;
    bool napt_enabled;
    bool reconnect_pending;
    char uplink_ip[16];
    char dns[16];
    char dial_status[48];
    char sim_status[32];
    char signal_csq[32];
    char cereg_status[64];
    char network_info[96];
    char module_model[32];
    char last_error[96];
    cellular_uplink_mode_t uplink_mode;
} cellular_status_t;

esp_err_t cellular_ecm_start(esp_netif_t *ap_netif);
void cellular_ecm_get_status(cellular_status_t *status);
esp_err_t cellular_ecm_request_reconnect(void);
esp_err_t cellular_ecm_apply_config(const app_config_t *config);
esp_err_t cellular_ecm_suspend(uint32_t timeout_ms);
esp_err_t cellular_ecm_resume(void);
bool cellular_ecm_is_suspended(void);
