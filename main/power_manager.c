#include "power_manager.h"

#include <inttypes.h>

#include "app_state.h"
#include "app_tasks.h"
#include "cellular_ecm.h"
#include "driver/temperature_sensor.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_ap.h"

#define POWER_SAMPLE_INTERVAL_MS          5000
#define POWER_IDLE_SLEEP_DELAY_MS         30000
#define POWER_IDLE_SLEEP_DURATION_MS      30000
#define POWER_THERMAL_WARN_C              50.0f
#define POWER_THERMAL_WARN_CLEAR_C        47.0f
#define POWER_THERMAL_SLEEP_C             60.0f
#define POWER_THERMAL_SLEEP_CLEAR_C       55.0f
#define POWER_THERMAL_SLEEP_DELAY_MS      60000
#define POWER_THERMAL_SLEEP_DURATION_MS   60000
#define POWER_ECM_SUSPEND_TIMEOUT_MS      15000

typedef enum {
    POWER_SLEEP_REASON_IDLE = 0,
    POWER_SLEEP_REASON_THERMAL = 1,
} power_sleep_reason_t;

static const char *TAG = "power_manager";
static TaskHandle_t s_power_task = NULL;
static temperature_sensor_handle_t s_temp_sensor = NULL;

static const char *sleep_reason_to_string(power_sleep_reason_t reason)
{
    switch (reason) {
    case POWER_SLEEP_REASON_IDLE:
        return "idle";
    case POWER_SLEEP_REASON_THERMAL:
        return "thermal";
    default:
        return "unknown";
    }
}

static bool read_internal_temperature(float *temp_celsius)
{
    if (s_temp_sensor == NULL || temp_celsius == NULL) {
        return false;
    }

    return temperature_sensor_get_celsius(s_temp_sensor, temp_celsius) == ESP_OK;
}

static bool idle_sleep_allowed(const app_state_snapshot_t *snapshot, const cellular_status_t *status)
{
    if (snapshot == NULL || status == NULL) {
        return false;
    }

    if (!snapshot->softap_started) {
        return false;
    }

    if (snapshot->connected_sta_count != 0) {
        return false;
    }

    if (!status->usb_connected || !status->at_ready || !status->uplink_connected) {
        return false;
    }

    if (status->reconnect_pending || cellular_ecm_is_suspended()) {
        return false;
    }

    return true;
}

static esp_err_t resume_router_services(void)
{
    app_config_t config;
    esp_err_t err;

    app_state_get_config(&config);

    err = wifi_ap_resume(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to resume SoftAP: %s", esp_err_to_name(err));
        return err;
    }

    err = cellular_ecm_resume();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to resume ECM: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static esp_err_t enter_light_sleep(power_sleep_reason_t reason, uint32_t sleep_ms, float current_temp)
{
    int64_t before_us = 0;
    int64_t after_us = 0;
    esp_err_t err;

    ESP_LOGW(TAG,
             "Entering light sleep, reason=%s sleep_ms=%" PRIu32 " temp=%.2fC",
             sleep_reason_to_string(reason),
             sleep_ms,
             current_temp);

    err = cellular_ecm_suspend(POWER_ECM_SUSPEND_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Suspend ECM failed: %s", esp_err_to_name(err));
        return err;
    }

    err = wifi_ap_suspend();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Suspend SoftAP failed: %s", esp_err_to_name(err));
        (void)cellular_ecm_resume();
        return err;
    }

    app_state_set_power_sleeping(true);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)sleep_ms * 1000ULL));
    before_us = esp_timer_get_time();
    err = esp_light_sleep_start();
    after_us = esp_timer_get_time();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Light sleep start failed: %s", esp_err_to_name(err));
    }
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

    app_state_set_power_sleeping(false);

    esp_err_t resume_err = resume_router_services();
    if (resume_err != ESP_OK) {
        return resume_err;
    }

    ESP_LOGI(TAG,
             "Returned from light sleep, reason=%s wake_causes=0x%" PRIx32 " slept_ms=%" PRIi64,
             sleep_reason_to_string(reason),
             esp_sleep_get_wakeup_causes(),
             (after_us - before_us) / 1000);

    return err;
}

static esp_err_t init_temperature_sensor(void)
{
    temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    config.flags.allow_pd = 0;

    ESP_RETURN_ON_ERROR(temperature_sensor_install(&config, &s_temp_sensor), TAG, "Install temperature sensor failed");
    ESP_RETURN_ON_ERROR(temperature_sensor_enable(s_temp_sensor), TAG, "Enable temperature sensor failed");
    return ESP_OK;
}

static void power_manager_task(void *arg)
{
    bool warn_active = false;
    int64_t idle_since_us = 0;
    int64_t thermal_since_us = 0;

    (void)arg;

    while (true) {
        float temp_celsius = 0.0f;
        app_state_snapshot_t snapshot;
        cellular_status_t status;
        int64_t now_us = esp_timer_get_time();
        bool temp_ok = read_internal_temperature(&temp_celsius);

        if (temp_ok) {
            app_state_set_internal_temp_celsius(temp_celsius);
        }

        app_state_get_snapshot(&snapshot);
        cellular_ecm_get_status(&status);

        if (temp_ok && temp_celsius >= POWER_THERMAL_WARN_C) {
            if (!warn_active) {
                ESP_LOGW(TAG, "Internal temperature warning: %.2fC", temp_celsius);
                warn_active = true;
            }
        } else if (warn_active && temp_ok && temp_celsius <= POWER_THERMAL_WARN_CLEAR_C) {
            ESP_LOGI(TAG, "Internal temperature recovered: %.2fC", temp_celsius);
            warn_active = false;
        }

        app_state_set_thermal_protect_active(warn_active);

        if (temp_ok && temp_celsius >= POWER_THERMAL_SLEEP_C) {
            if (thermal_since_us == 0) {
                thermal_since_us = now_us;
                ESP_LOGW(TAG, "Thermal protect timer started at %.2fC", temp_celsius);
            }
        } else if (!temp_ok || temp_celsius <= POWER_THERMAL_SLEEP_CLEAR_C) {
            thermal_since_us = 0;
        }

        if (thermal_since_us != 0 &&
            now_us - thermal_since_us >= (int64_t)POWER_THERMAL_SLEEP_DELAY_MS * 1000) {
            (void)enter_light_sleep(POWER_SLEEP_REASON_THERMAL,
                                    POWER_THERMAL_SLEEP_DURATION_MS,
                                    temp_ok ? temp_celsius : 0.0f);
            idle_since_us = 0;
            thermal_since_us = 0;
            warn_active = false;
            app_state_set_thermal_protect_active(false);
            vTaskDelay(pdMS_TO_TICKS(POWER_SAMPLE_INTERVAL_MS));
            continue;
        }

        if (idle_sleep_allowed(&snapshot, &status)) {
            if (idle_since_us == 0) {
                idle_since_us = now_us;
                ESP_LOGI(TAG, "Idle timer started");
            } else if (now_us - idle_since_us >= (int64_t)POWER_IDLE_SLEEP_DELAY_MS * 1000) {
                (void)enter_light_sleep(POWER_SLEEP_REASON_IDLE,
                                        POWER_IDLE_SLEEP_DURATION_MS,
                                        temp_ok ? temp_celsius : 0.0f);
                idle_since_us = 0;
                thermal_since_us = 0;
                warn_active = false;
                app_state_set_thermal_protect_active(false);
                vTaskDelay(pdMS_TO_TICKS(POWER_SAMPLE_INTERVAL_MS));
                continue;
            }
        } else {
            idle_since_us = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(POWER_SAMPLE_INTERVAL_MS));
    }
}

esp_err_t power_manager_start(void)
{
    BaseType_t task_ok;

    if (s_power_task != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(init_temperature_sensor(), TAG, "Failed to initialize temperature sensor");

    task_ok = xTaskCreatePinnedToCore(power_manager_task,
                                      "power_manager",
                                      4096,
                                      NULL,
                                      APP_TASK_PRIO_POWER_MANAGER,
                                      &s_power_task,
                                      APP_CORE_BACKGROUND);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "Failed to create power manager task");
    return ESP_OK;
}
