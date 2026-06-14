#include "power_manager.h"

#include "app_state.h"
#include "app_tasks.h"
#include "driver/temperature_sensor.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_thermal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POWER_SAMPLE_INTERVAL_MS          5000
#define POWER_THERMAL_WARN_C              55.0f
#define POWER_THERMAL_WARN_CLEAR_C        50.0f
#define POWER_THERMAL_DANGER_C            60.0f
#define POWER_THERMAL_DANGER_CLEAR_C      55.0f

static const char *TAG = "power_manager";
static TaskHandle_t s_power_task = NULL;
static temperature_sensor_handle_t s_temp_sensor = NULL;
static bool s_danger_active = false;

static bool read_internal_temperature(float *temp_celsius)
{
    if (s_temp_sensor == NULL || temp_celsius == NULL) {
        return false;
    }

    return temperature_sensor_get_celsius(s_temp_sensor, temp_celsius) == ESP_OK;
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

    (void)arg;

    while (true) {
        float temp_celsius = 0.0f;
        bool temp_ok = read_internal_temperature(&temp_celsius);

        if (temp_ok) {
            app_state_set_internal_temp_celsius(temp_celsius);
        }

        /* Thermal warning: 55C enter, 50C clear */
        if (temp_ok && temp_celsius >= POWER_THERMAL_WARN_C) {
            if (!warn_active) {
                ESP_LOGW(TAG, "Temperature warning: %.2fC", temp_celsius);
                warn_active = true;
            }
        } else if (warn_active && temp_ok && temp_celsius <= POWER_THERMAL_WARN_CLEAR_C) {
            ESP_LOGI(TAG, "Temperature recovered: %.2fC", temp_celsius);
            warn_active = false;
        }

        /* Thermal danger: >=60C log critical (LED already breathing from 55C) */
        if (temp_ok && temp_celsius >= POWER_THERMAL_DANGER_C) {
            if (!s_danger_active) {
                ESP_LOGE(TAG, "CRITICAL: Chip temperature %.2fC exceeds safe limit!", temp_celsius);
                s_danger_active = true;
            }
        } else if (s_danger_active && temp_ok && temp_celsius <= POWER_THERMAL_DANGER_CLEAR_C) {
            ESP_LOGI(TAG, "Temperature back to safe range: %.2fC", temp_celsius);
            s_danger_active = false;
        }

        app_state_set_thermal_protect_active(warn_active);

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
                                      APP_TASK_STACK_POWER_MANAGER,
                                      NULL,
                                      APP_TASK_PRIO_POWER_MANAGER,
                                      &s_power_task,
                                      APP_CORE_BACKGROUND);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "Failed to create power manager task");
    return ESP_OK;
}
