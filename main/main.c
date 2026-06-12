/*
 * ESP32S3_ECM_V1 鍚姩鍏ュ彛
 * 褰撳墠鐗堟湰锛歏1.1
 * 璇存槑锛氫粎璐熻矗鏈€灏?ECM 璺敱闂幆鐨勫垵濮嬪寲缂栨帓銆? */

#include "app_config.h"
#include "app_state.h"
#include "cellular_ecm.h"
#include "esp_log.h"
#include "power_manager.h"
#include "wifi_ap.h"

static const char *TAG = "ESP32S3_ECM_V1";

const char *app_get_version(void)
{
    return "V1.2.2";
}

void app_main(void)
{
    app_config_t config;
    esp_err_t err;

    ESP_LOGI(TAG, "ESP32S3_ECM_V1 minimal ECM router start, version=%s", app_get_version());

    app_state_init();
    ESP_ERROR_CHECK(app_config_nvs_init());
    ESP_ERROR_CHECK(app_config_load(&config));
    app_state_set_config(&config);

    ESP_ERROR_CHECK(wifi_ap_init());
    ESP_ERROR_CHECK(wifi_ap_apply_config(&config));
    ESP_ERROR_CHECK(cellular_ecm_start(wifi_ap_get_ap_netif()));
    ESP_ERROR_CHECK(cellular_ecm_apply_config(&config));
    err = power_manager_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Power manager disabled: %s", esp_err_to_name(err));
    }
}
