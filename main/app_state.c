/*

 * ESP32S3_ECM_V1 运行状态实现

 * 当前版本：V1.2.2

 * 说明：提供线程安全的共享状态读写。

 */



#include "app_state.h"



#include <assert.h>



#include "freertos/FreeRTOS.h"

#include "freertos/semphr.h"



static SemaphoreHandle_t s_state_mutex = NULL;

static app_state_snapshot_t s_state = { 0 };



static void app_state_lock(void)

{

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);

}



static void app_state_unlock(void)

{

    xSemaphoreGive(s_state_mutex);

}



void app_state_init(void)

{

    s_state_mutex = xSemaphoreCreateMutex();

    assert(s_state_mutex != NULL);



    app_config_set_defaults(&s_state.config);

    s_state.runtime_channel = APP_WIFI_DEFAULT_CHANNEL;

}



void app_state_set_config(const app_config_t *config)

{

    if (config == NULL) {

        return;

    }



    app_state_lock();

    s_state.config = *config;

    app_state_unlock();

}



void app_state_get_config(app_config_t *config)

{

    if (config == NULL) {

        return;

    }



    app_state_lock();

    *config = s_state.config;

    app_state_unlock();

}



void app_state_set_softap_started(bool started)

{

    app_state_lock();

    s_state.softap_started = started;

    app_state_unlock();

}



void app_state_set_connected_sta_count(uint8_t connected_sta_count)

{

    app_state_lock();

    s_state.connected_sta_count = connected_sta_count;

    app_state_unlock();

}



void app_state_set_runtime_channel(uint8_t runtime_channel)

{

    app_state_lock();

    s_state.runtime_channel = runtime_channel;

    app_state_unlock();

}



void app_state_set_power_sleeping(bool sleeping)

{

    app_state_lock();

    s_state.power_sleeping = sleeping;

    app_state_unlock();

}



void app_state_set_thermal_protect_active(bool active)

{

    app_state_lock();

    s_state.thermal_protect_active = active;

    app_state_unlock();

}



void app_state_set_internal_temp_celsius(float internal_temp_celsius)

{

    app_state_lock();

    s_state.internal_temp_celsius = internal_temp_celsius;

    app_state_unlock();

}



bool app_state_is_softap_started(void)
{
    bool result;
    app_state_lock();
    result = s_state.softap_started;
    app_state_unlock();
    return result;
}

uint8_t app_state_get_sta_count(void)
{
    uint8_t result;
    app_state_lock();
    result = s_state.connected_sta_count;
    app_state_unlock();
    return result;
}

void app_state_get_snapshot(app_state_snapshot_t *snapshot)

{

    if (snapshot == NULL) {

        return;

    }



    app_state_lock();

    *snapshot = s_state;

    app_state_unlock();

}

