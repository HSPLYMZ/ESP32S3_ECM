/* Main entry for the minimal ECM router. */

#include "app_config.h"
#include "app_state.h"
#include "cellular_ecm.h"
#include "diag_system.h"
#include "esp_log.h"
#include "esp_system.h"
#include "fault_log.h"
#include "led_thermal.h"
#include "power_manager.h"
#include "webui.h"
#include "wifi_ap.h"

static const char *TAG = "ESP32S3_ECM_V1";

const char *app_get_version(void)
{
    return "V1.3.0";
}

void app_main(void)
{
    app_config_t config;
    esp_err_t err;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32S3_ECM_V1 4G-to-WiFi Router");
    ESP_LOGI(TAG, "  Version: %s", app_get_version());
    ESP_LOGI(TAG, "  Chip: ESP32-S3, 8MB PSRAM, 16MB Flash");
    ESP_LOGI(TAG, "  Modem: Quectel EC200A-CN (ECM mode)");
    ESP_LOGI(TAG, "  Target: esp32s3 (IDF v6.0.1)");
    ESP_LOGI(TAG, "========================================");

    app_state_init();
    ESP_ERROR_CHECK(app_config_nvs_init());
    err = fault_log_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Fault log disabled: %s", esp_err_to_name(err));
    } else {
        fault_log_record(FAULT_LOG_LEVEL_INFO,
                         "system",
                         "boot",
                         "version=%s reset=%d",
                         app_get_version(),
                         (int)esp_reset_reason());
    }
    ESP_ERROR_CHECK(app_config_load(&config));
    app_state_set_config(&config);

    ESP_ERROR_CHECK(wifi_ap_init());
    ESP_ERROR_CHECK(wifi_ap_apply_config(&config));
    ESP_ERROR_CHECK(cellular_ecm_start(wifi_ap_get_ap_netif()));
    ESP_ERROR_CHECK(cellular_ecm_apply_config(&config));

    err = webui_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WebUI disabled: %s", esp_err_to_name(err));
        fault_log_record(FAULT_LOG_LEVEL_WARN, "system", "webui_fail", "%s", esp_err_to_name(err));
    }

    err = diag_system_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Diagnostics disabled: %s", esp_err_to_name(err));
        fault_log_record(FAULT_LOG_LEVEL_WARN, "system", "diag_fail", "%s", esp_err_to_name(err));
    }

    err = led_thermal_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Thermal LED disabled: %s", esp_err_to_name(err));
        fault_log_record(FAULT_LOG_LEVEL_WARN, "system", "led_fail", "%s", esp_err_to_name(err));
    }
    err = power_manager_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Power manager disabled: %s", esp_err_to_name(err));
        fault_log_record(FAULT_LOG_LEVEL_WARN, "system", "power_fail", "%s", esp_err_to_name(err));
    }
}
