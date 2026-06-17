idf_component_register(
    SRCS
        "app_config.c"
        "app_state.c"
        "cellular_ecm.c"
        "diag_system.c"
        "fault_log.c"
        "led_thermal.c"
        "main.c"
        "power_manager.c"
        "webui.c"
        "wifi_ap.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi esp_event esp_netif esp_timer nvs_flash esp_driver_tsens esp_driver_rmt espressif__iot_eth espressif__iot_usbh_cdc espressif__iot_usbh_ecm
)
