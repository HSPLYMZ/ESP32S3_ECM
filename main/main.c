/* Main entry for the minimal ECM router. */

#include "app_config.h"
#include "app_state.h"
#include "cellular_ecm.h"
#include "esp_log.h"
#include "power_manager.h"
#include "led_thermal.h"
#include "wifi_ap.h"

static const char *TAG = "ESP32S3_ECM_V1";

const char *app_get_version(void)
{
    return "V1.2.5";
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
    ESP_ERROR_CHECK(app_config_load(&config));
    app_state_set_config(&config);

    ESP_ERROR_CHECK(wifi_ap_init());
    ESP_ERROR_CHECK(wifi_ap_apply_config(&config));
    ESP_ERROR_CHECK(cellular_ecm_start(wifi_ap_get_ap_netif()));
    ESP_ERROR_CHECK(cellular_ecm_apply_config(&config));

    ESP_ERROR_CHECK(led_thermal_init());
    err = power_manager_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Power manager disabled: %s", esp_err_to_name(err));
    }
}
