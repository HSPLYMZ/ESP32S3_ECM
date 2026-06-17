/*

 * ESP32S3_ECM_V1 - EC200A ECM ????

 * ?????V1.2.3

 * ???EC200A USB CDC/ECM ???? AT ????ECM ???DNS ???NAPT????????

 */

#include "cellular_ecm.h"

#include <inttypes.h>

#include <stdio.h>

#include <string.h>

#include "app_tasks.h"

#include "esp_check.h"

#include "esp_event.h"

#include "esp_log.h"

#include "esp_netif.h"

#include "esp_netif_ip_addr.h"

#include "esp_system.h"

#include "esp_timer.h"
#include "fault_log.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"

#include "freertos/event_groups.h"

#include "freertos/portmacro.h"
#include "freertos/semphr.h"

#include "freertos/task.h"

#include "iot_eth.h"

#include "iot_eth_netif_glue.h"

#include "iot_usbh_cdc.h"

#include "iot_usbh_ecm.h"

#include "lwip/lwip_napt.h"

#include "usb/usb_host.h"

#include "wifi_ap.h"

#define EC200A_USB_VID 0x2C7C

#define EC200A_USB_PID 0x6005

#define EC200A_AT_INTERFACE 3

#define AT_RX_BUFFER_SIZE 2048

#define AT_TRANSFER_BUFFER_SIZE 512

#define AT_PORT_WRITE_TIMEOUT_MS 1000

#define AT_RESPONSE_TIMEOUT_MS 3000

#define AT_RECOVERY_DELAY_MS 500

#define AT_PORT_OPEN_RETRY_COUNT 10

#define AT_PORT_OPEN_RETRY_DELAY_MS 500

#define AT_READY_RETRY_COUNT 3

#define CGATT_WAIT_RETRY_COUNT 45

#define CGATT_WAIT_INTERVAL_MS 1000

#define CGATT_ACTIVE_ATTACH_ATTEMPT 5

#define CGATT_STATUS_LOG_INTERVAL 5

#define ECM_READY_POLL_MS 200

#define ECM_IP_WAIT_TIMEOUT_MS 30000

#define ECM_RECONNECT_DELAY_MS 250

#define ECM_START_RETRY_DELAY_MS 2000

#define ECM_FAILURES_BEFORE_HOST_RESTART 5

#define ECM_REQ_RECONNECT BIT(0)

#define ECM_REQ_RESTART    BIT(1)

#define ECM_RUNTIME_LINK_UP BIT(0)

#define ECM_RUNTIME_IP_UP   BIT(1)

#define ECM_DIAG_NAMESPACE "ecm_diag"

#define ECM_DIAG_KEY_RECONNECTS "reconnects"

#define ECM_DIAG_KEY_FAILURES "failures"

#define ECM_DIAG_KEY_LAST_FAILURE "last_fail"

#define ECM_DIAG_KEY_CGATT "cgatt"

static const char *TAG = "cellular_ecm";

static const usb_device_match_id_t s_ecm_match_ids[] = {

    {

        .match_flags = USB_DEVICE_ID_MATCH_VID_PID,

        .idVendor = EC200A_USB_VID,

        .idProduct = EC200A_USB_PID,

    },

    { 0 },

};

static esp_netif_t *s_ap_netif = NULL;

static esp_netif_t *s_ecm_netif = NULL;

static iot_eth_driver_t *s_ecm_driver = NULL;

static iot_eth_handle_t s_eth_handle = NULL;

static iot_eth_netif_glue_handle_t s_netif_glue = NULL;

static usbh_cdc_port_handle_t s_at_port = NULL;

static EventGroupHandle_t s_request_events = NULL;

static EventGroupHandle_t s_runtime_events = NULL;

static TaskHandle_t s_manager_task = NULL;

static app_config_t s_runtime_config = { 0 };

static cellular_status_t s_status = { 0 };

static SemaphoreHandle_t s_status_mutex = NULL;

static bool s_cdc_driver_ready = false;

static bool s_ecm_started = false;

static bool s_ip_acquired = false;

static bool s_runtime_stopping = false;

static volatile uint8_t s_usb_dev_addr = 0;

static uint8_t s_consecutive_bringup_failures = 0;

static void update_radio_status_snapshot(void);


/* ================================================================
 * SECTION: Internal Helpers (status get/set, diagnostics, NAPT, DNS)
 * ================================================================ */

static void status_set_text_field(char *field, size_t field_len, const char *value)

{

    if (field == NULL || field_len == 0) {

        return;

    }

    if (value == NULL || value[0] == '\0') {

        strlcpy(field, "--", field_len);

    } else {

        strlcpy(field, value, field_len);

    }

}

static void status_set_defaults(void)

{

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    memset(&s_status, 0, sizeof(s_status));

    s_status.uplink_mode = CELLULAR_UPLINK_MODE_ECM;

    status_set_text_field(s_status.dial_status, sizeof(s_status.dial_status), "Waiting for ECM USB device");

    status_set_text_field(s_status.sim_status, sizeof(s_status.sim_status), "--");

    status_set_text_field(s_status.signal_csq, sizeof(s_status.signal_csq), "--");

    status_set_text_field(s_status.cereg_status, sizeof(s_status.cereg_status), "--");

    status_set_text_field(s_status.cgatt_status, sizeof(s_status.cgatt_status), "--");

    status_set_text_field(s_status.network_info, sizeof(s_status.network_info), "--");

    status_set_text_field(s_status.module_model, sizeof(s_status.module_model), "EC200A");

    status_set_text_field(s_status.last_error, sizeof(s_status.last_error), "Waiting for EC200A");
    status_set_text_field(s_status.last_failure, sizeof(s_status.last_failure), "--");
    xSemaphoreGive(s_status_mutex);

}

static void status_set_usb_connected(bool connected)

{

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    s_status.usb_connected = connected;

    if (!connected) {

        s_status.at_ready = false;

        s_status.uplink_connected = false;

        s_status.napt_enabled = false;

        s_status.uplink_ip[0] = '\0';

        s_status.dns[0] = '\0';

        status_set_text_field(s_status.sim_status, sizeof(s_status.sim_status), "--");

        status_set_text_field(s_status.signal_csq, sizeof(s_status.signal_csq), "--");

        status_set_text_field(s_status.cereg_status, sizeof(s_status.cereg_status), "--");

        status_set_text_field(s_status.network_info, sizeof(s_status.network_info), "--");

    }

    xSemaphoreGive(s_status_mutex);

}

static void status_set_at_ready(bool ready)

{

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    s_status.at_ready = ready;

    xSemaphoreGive(s_status_mutex);

}

static void status_set_uplink_connected(bool connected)

{

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    s_status.uplink_connected = connected;

    if (!connected) {

        s_status.napt_enabled = false;

        s_status.uplink_ip[0] = '\0';

        s_status.dns[0] = '\0';

    }

    xSemaphoreGive(s_status_mutex);

}

static void status_set_reconnect_pending(bool pending)

{

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    s_status.reconnect_pending = pending;

    xSemaphoreGive(s_status_mutex);

}

static void status_set_dial_status(const char *value)

{

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    status_set_text_field(s_status.dial_status, sizeof(s_status.dial_status), value);

    xSemaphoreGive(s_status_mutex);

}

static void status_set_last_error(const char *value)

{

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    status_set_text_field(s_status.last_error, sizeof(s_status.last_error), value);

    xSemaphoreGive(s_status_mutex);

}

static void status_get_last_error(char *value, size_t value_len)
{
    if (value == NULL || value_len == 0) {
        return;
    }

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    strlcpy(value, s_status.last_error, value_len);
    xSemaphoreGive(s_status_mutex);
}

static void status_set_ip_dns(const char *ip, const char *dns)

{

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    strlcpy(s_status.uplink_ip, ip != NULL ? ip : "", sizeof(s_status.uplink_ip));

    strlcpy(s_status.dns, dns != NULL ? dns : "", sizeof(s_status.dns));

    xSemaphoreGive(s_status_mutex);

}

static void status_set_sim_status(const char *value)

{

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    status_set_text_field(s_status.sim_status, sizeof(s_status.sim_status), value);

    xSemaphoreGive(s_status_mutex);

}

static void status_set_signal_csq(const char *value)

{

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    status_set_text_field(s_status.signal_csq, sizeof(s_status.signal_csq), value);

    xSemaphoreGive(s_status_mutex);

}

static void status_set_cereg_status(const char *value)

{

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    status_set_text_field(s_status.cereg_status, sizeof(s_status.cereg_status), value);

    xSemaphoreGive(s_status_mutex);

}

static void status_set_network_info(const char *value)

{

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    status_set_text_field(s_status.network_info, sizeof(s_status.network_info), value);

    xSemaphoreGive(s_status_mutex);

}

static void persistent_diag_save(void)
{
    cellular_status_t snapshot;
    nvs_handle_t handle;
    esp_err_t err;

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    snapshot = s_status;
    xSemaphoreGive(s_status_mutex);

    err = nvs_open(ECM_DIAG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open persistent ECM diagnostics: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_u32(handle, ECM_DIAG_KEY_RECONNECTS, snapshot.reconnect_count);
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, ECM_DIAG_KEY_FAILURES, snapshot.failure_count);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, ECM_DIAG_KEY_LAST_FAILURE, snapshot.last_failure);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, ECM_DIAG_KEY_CGATT, snapshot.cgatt_status);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist ECM diagnostics: %s", esp_err_to_name(err));
    }
}

static void persistent_diag_load(void)
{
    cellular_status_t stored = { 0 };
    nvs_handle_t handle;
    size_t length;

    if (nvs_open(ECM_DIAG_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    (void)nvs_get_u32(handle, ECM_DIAG_KEY_RECONNECTS, &stored.reconnect_count);
    (void)nvs_get_u32(handle, ECM_DIAG_KEY_FAILURES, &stored.failure_count);
    length = sizeof(stored.last_failure);
    (void)nvs_get_str(handle, ECM_DIAG_KEY_LAST_FAILURE, stored.last_failure, &length);
    length = sizeof(stored.cgatt_status);
    (void)nvs_get_str(handle, ECM_DIAG_KEY_CGATT, stored.cgatt_status, &length);
    nvs_close(handle);

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_status.reconnect_count = stored.reconnect_count;
    s_status.failure_count = stored.failure_count;
    if (stored.last_failure[0] != '\0') {
        strlcpy(s_status.last_failure, stored.last_failure, sizeof(s_status.last_failure));
    }
    if (stored.cgatt_status[0] != '\0') {
        strlcpy(s_status.cgatt_status, stored.cgatt_status, sizeof(s_status.cgatt_status));
    }
    xSemaphoreGive(s_status_mutex);

    ESP_LOGI(TAG,
             "Loaded persistent diagnostics: reconnects=%" PRIu32 " failures=%" PRIu32 " cgatt=%s",
             stored.reconnect_count,
             stored.failure_count,
             stored.cgatt_status[0] != '\0' ? stored.cgatt_status : "--");
}

static void persistent_diag_record_reconnect(const char *failure_reason)
{
    uint32_t reconnect_count;
    uint32_t failure_count;
    bool has_failure = failure_reason != NULL && failure_reason[0] != '\0' && strcmp(failure_reason, "--") != 0;

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_status.reconnect_count++;
    if (has_failure) {
        s_status.failure_count++;
        strlcpy(s_status.last_failure, failure_reason, sizeof(s_status.last_failure));
    }
    reconnect_count = s_status.reconnect_count;
    failure_count = s_status.failure_count;
    xSemaphoreGive(s_status_mutex);
    ESP_LOGW(TAG,
             "Persistent reconnect count=%" PRIu32 " failure count=%" PRIu32,
             reconnect_count,
             failure_count);
    fault_log_record(has_failure ? FAULT_LOG_LEVEL_WARN : FAULT_LOG_LEVEL_INFO,
                     "ecm",
                     "reconnect",
                     "reconnects=%" PRIu32 " failures=%" PRIu32 " reason=%s",
                     reconnect_count,
                     failure_count,
                     has_failure ? failure_reason : "no_failure_detail");
    persistent_diag_save();
}

static void persistent_diag_set_cgatt(const char *value)
{
    bool changed = false;

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    if (value != NULL && value[0] != '\0' && strcmp(s_status.cgatt_status, value) != 0) {
        strlcpy(s_status.cgatt_status, value, sizeof(s_status.cgatt_status));
        changed = true;
    }
    xSemaphoreGive(s_status_mutex);

    if (changed) {
        persistent_diag_save();
    }
}

static esp_err_t enable_softap_napt(void)

{

#if IP_NAPT

    if (s_ap_netif == NULL) {

        return ESP_ERR_INVALID_STATE;

    }

    esp_err_t err = esp_netif_napt_enable(s_ap_netif);

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    s_status.napt_enabled = (err == ESP_OK);

    xSemaphoreGive(s_status_mutex);

    if (err == ESP_OK) {

        ESP_LOGI(TAG, "NAPT enabled on SoftAP for ECM uplink");

    } else {

        ESP_LOGW(TAG, "Failed to enable SoftAP NAPT: %s", esp_err_to_name(err));

    }

    return err;

#else

    return ESP_ERR_NOT_SUPPORTED;

#endif

}

/* ================================================================
 * SECTION: AT Communication Layer (port I/O, command/response, parser)
 * ================================================================ */

static void compact_spaces(char *text)

{

    char *read_ptr = text;

    char *write_ptr = text;

    bool last_space = false;

    while (read_ptr != NULL && *read_ptr != '\0') {

        char current = *read_ptr++;

        bool is_space = (current == '\r' || current == '\n' || current == '\t' || current == ' ');

        if (is_space) {

            if (!last_space && write_ptr != text) {

                *write_ptr++ = ' ';

            }

            last_space = true;

        } else {

            *write_ptr++ = current;

            last_space = false;

        }

    }

    if (write_ptr > text && write_ptr[-1] == ' ') {

        write_ptr--;

    }

    *write_ptr = '\0';

}

static bool extract_prefixed_value(const char *response,

                                   const char *prefix,

                                   char *value,

                                   size_t value_len)

{

    const char *begin = strstr(response, prefix);

    const char *end = NULL;

    size_t copy_len = 0;

    if (begin == NULL || value == NULL || value_len == 0) {

        return false;

    }

    begin += strlen(prefix);

    while (*begin == ' ' || *begin == '\t') {

        begin++;

    }

    end = strpbrk(begin, "\r\n");

    if (end == NULL) {

        end = begin + strlen(begin);

    }

    copy_len = (size_t)(end - begin);

    if (copy_len >= value_len) {

        copy_len = value_len - 1;

    }

    memcpy(value, begin, copy_len);

    value[copy_len] = '\0';

    compact_spaces(value);

    return value[0] != '\0';

}

static esp_err_t at_port_write_line(const char *command)

{

    if (s_at_port == NULL || command == NULL) {

        return ESP_ERR_INVALID_STATE;

    }

    return usbh_cdc_write_bytes(s_at_port,

                                (const uint8_t *)command,

                                strlen(command),

                                pdMS_TO_TICKS(AT_PORT_WRITE_TIMEOUT_MS));

}

static esp_err_t at_port_read_response(char *response, size_t response_len, uint32_t timeout_ms)

{

    int64_t deadline_us = esp_timer_get_time() + ((int64_t)timeout_ms * 1000);

    size_t used = 0;

    if (response == NULL || response_len == 0) {

        return ESP_ERR_INVALID_ARG;

    }

    response[0] = '\0';

    while (esp_timer_get_time() < deadline_us) {

        size_t available = 0;

        if (usbh_cdc_get_rx_buffer_size(s_at_port, &available) != ESP_OK || available == 0) {

            vTaskDelay(pdMS_TO_TICKS(20));

            continue;

        }

        size_t chunk_len = available;

        uint8_t chunk[128];

        if (chunk_len > sizeof(chunk)) {

            chunk_len = sizeof(chunk);

        }

        if (usbh_cdc_read_bytes(s_at_port, chunk, &chunk_len, 0) != ESP_OK || chunk_len == 0) {

            vTaskDelay(pdMS_TO_TICKS(20));

            continue;

        }

        size_t copy = chunk_len;

        if (copy > response_len - used - 1) {

            copy = response_len - used - 1;

        }

        if (copy > 0) {

            memcpy(response + used, chunk, copy);

            used += copy;

            response[used] = '\0';

        }

        if (strstr(response, "\r\nOK\r\n") != NULL || strstr(response, "\r\nERROR\r\n") != NULL) {

            return ESP_OK;

        }

    }

    return ESP_ERR_TIMEOUT;

}

static esp_err_t send_at_capture(const char *command,

                                 char *response,

                                 size_t response_len,

                                 uint32_t timeout_ms)

{

    if (s_at_port == NULL) {

        return ESP_ERR_INVALID_STATE;

    }

    (void)usbh_cdc_flush_rx_buffer(s_at_port);

    ESP_RETURN_ON_ERROR(at_port_write_line(command), TAG, "AT write failed");

    return at_port_read_response(response, response_len, timeout_ms);

}

static esp_err_t send_at_expect_ok(const char *command, uint32_t timeout_ms)

{

    char response[512];

    esp_err_t err = send_at_capture(command, response, sizeof(response), timeout_ms);

    if (err != ESP_OK) {

        return err;

    }

    return strstr(response, "\r\nOK\r\n") != NULL ? ESP_OK : ESP_FAIL;

}

static esp_err_t ensure_at_ready(void)

{

    esp_err_t err = ESP_FAIL;

    for (int attempt = 0; attempt < AT_READY_RETRY_COUNT; ++attempt) {

        ESP_LOGI(TAG, "AT handshake attempt %d/%d", attempt + 1, AT_READY_RETRY_COUNT);

        err = send_at_expect_ok("AT\r", AT_RESPONSE_TIMEOUT_MS);

        if (err == ESP_OK) {

            err = send_at_expect_ok("ATE0\r", AT_RESPONSE_TIMEOUT_MS);

            if (err == ESP_OK) {

                ESP_LOGI(TAG, "AT ready");

            }

            return err;

        }

        ESP_LOGW(TAG, "AT handshake attempt %d/%d failed", attempt + 1, AT_READY_RETRY_COUNT);

        vTaskDelay(pdMS_TO_TICKS(AT_RECOVERY_DELAY_MS));

    }

    return err;

}

static esp_err_t wait_for_packet_attach(void)

{

    char response[256];
    bool active_attach_sent = false;

    for (int attempt = 0; attempt < CGATT_WAIT_RETRY_COUNT; ++attempt) {

        EventBits_t runtime_bits = s_runtime_events != NULL ? xEventGroupGetBits(s_runtime_events) : 0;
        if ((runtime_bits & ECM_RUNTIME_IP_UP) != 0) {
            ESP_LOGI(TAG, "ECM IP arrived while waiting for packet attach");
            return ESP_OK;
        }

        if (send_at_capture("AT+CGATT?\r", response, sizeof(response), AT_RESPONSE_TIMEOUT_MS) == ESP_OK) {
            char cgatt[16];

            if (extract_prefixed_value(response, "+CGATT:", cgatt, sizeof(cgatt))) {
                persistent_diag_set_cgatt(cgatt);
            }

            if (strstr(response, "+CGATT: 1") != NULL) {

                return ESP_OK;

            }

        }

        ESP_LOGW(TAG, "Packet attach not ready yet, attempt %d/%d", attempt + 1, CGATT_WAIT_RETRY_COUNT);

        if (!active_attach_sent && attempt + 1 >= CGATT_ACTIVE_ATTACH_ATTEMPT) {
            esp_err_t attach_err = send_at_expect_ok("AT+CGATT=1\r", 10000);
            ESP_LOGI(TAG, "Active packet attach result: %s", esp_err_to_name(attach_err));
            active_attach_sent = true;
        }

        if (((attempt + 1) % CGATT_STATUS_LOG_INTERVAL) == 0) {
            update_radio_status_snapshot();
        }

        vTaskDelay(pdMS_TO_TICKS(CGATT_WAIT_INTERVAL_MS));

    }

    return ESP_ERR_TIMEOUT;

}

static void update_radio_status_snapshot(void)

{

    static char response[512];
    static char parsed[128];

    if (send_at_capture("AT+CPIN?\r", response, sizeof(response), AT_RESPONSE_TIMEOUT_MS) == ESP_OK &&

        extract_prefixed_value(response, "+CPIN:", parsed, sizeof(parsed))) {

        status_set_sim_status(parsed);

    } else {

        status_set_sim_status("Read failed");

    }

    if (send_at_capture("AT+CSQ\r", response, sizeof(response), AT_RESPONSE_TIMEOUT_MS) == ESP_OK &&

        extract_prefixed_value(response, "+CSQ:", parsed, sizeof(parsed))) {

        status_set_signal_csq(parsed);

    } else {

        status_set_signal_csq("Read failed");

    }

    if (send_at_capture("AT+CEREG?\r", response, sizeof(response), AT_RESPONSE_TIMEOUT_MS) == ESP_OK &&

        extract_prefixed_value(response, "+CEREG:", parsed, sizeof(parsed))) {

        status_set_cereg_status(parsed);

    } else {

        status_set_cereg_status("Read failed");

    }

    if (send_at_capture("AT+CGATT?\r", response, sizeof(response), AT_RESPONSE_TIMEOUT_MS) == ESP_OK &&

        extract_prefixed_value(response, "+CGATT:", parsed, sizeof(parsed))) {

        persistent_diag_set_cgatt(parsed);

    }

    if (send_at_capture("AT+QNWINFO\r", response, sizeof(response), AT_RESPONSE_TIMEOUT_MS) == ESP_OK &&

        extract_prefixed_value(response, "+QNWINFO:", parsed, sizeof(parsed))) {

        status_set_network_info(parsed);

    } else {

        status_set_network_info("Read failed");

    }

}

static esp_err_t configure_modem_for_ecm(void)

{

    char apn_cmd[96];

    esp_err_t err;

    ESP_LOGI(TAG, "Configuring modem usbnet ECM mode");

    err = send_at_expect_ok("AT+QCFG=\"usbnet\",1\r", AT_RESPONSE_TIMEOUT_MS);

    ESP_RETURN_ON_ERROR(err, TAG, "Failed to set EC200A usbnet ECM mode");

    snprintf(apn_cmd,

             sizeof(apn_cmd),

             "AT+CGDCONT=1,\"IP\",\"%s\"\r",

             app_config_get_effective_apn(&s_runtime_config));

    ESP_LOGI(TAG, "Applying APN: %s", app_config_get_effective_apn(&s_runtime_config));

    err = send_at_expect_ok(apn_cmd, AT_RESPONSE_TIMEOUT_MS);

    ESP_RETURN_ON_ERROR(err, TAG, "Failed to set APN");

    ESP_LOGI(TAG, "Requesting ECM data session via QNETDEVCTL");

    err = send_at_expect_ok("AT+QNETDEVCTL=1,1,1\r", AT_RESPONSE_TIMEOUT_MS);

    if (err != ESP_OK) {

        ESP_LOGW(TAG, "QNETDEVCTL start returned: %s", esp_err_to_name(err));

    }

    ESP_LOGI(TAG, "Waiting for packet attach");

    return wait_for_packet_attach();

}

static void at_port_recv_cb(usbh_cdc_port_handle_t port_handle, void *user_data)

{

    (void)port_handle;

    (void)user_data;

}

static void at_port_closed_cb(usbh_cdc_port_handle_t port_handle, void *user_data)

{

    (void)port_handle;

    (void)user_data;

    s_at_port = NULL;

    status_set_usb_connected(false);

    status_set_at_ready(false);

    status_set_uplink_connected(false);

    status_set_dial_status("AT control closed");

    status_set_last_error("EC200A AT control interface disconnected.");
    fault_log_record(FAULT_LOG_LEVEL_WARN, "ecm", "at_closed", "AT control interface disconnected");

    if (!s_runtime_stopping && s_request_events != NULL) {

        xEventGroupSetBits(s_request_events, ECM_REQ_RESTART);

    }

}

static esp_err_t open_at_port(void)

{

    esp_err_t err = ESP_FAIL;

    for (int attempt = 0; attempt < AT_PORT_OPEN_RETRY_COUNT; ++attempt) {

        usbh_cdc_port_config_t port_cfg = {

            .dev_addr = s_usb_dev_addr,

            .itf_num = EC200A_AT_INTERFACE,

            .in_ringbuf_size = AT_RX_BUFFER_SIZE,

            .out_ringbuf_size = 0,

            .in_transfer_buffer_size = AT_TRANSFER_BUFFER_SIZE,

            .out_transfer_buffer_size = AT_TRANSFER_BUFFER_SIZE,

            .cbs = {

                .closed = at_port_closed_cb,

                .recv_data = at_port_recv_cb,

                .user_data = NULL,

            },

        };

        err = usbh_cdc_port_open(&port_cfg, &s_at_port);

        if (err == ESP_OK) {

            ESP_LOGI(TAG, "AT port opened on interface %d", EC200A_AT_INTERFACE);

            return ESP_OK;

        }

        ESP_LOGW(TAG,

                 "Open AT port failed on interface %d, attempt %d/%d: %s",

                 EC200A_AT_INTERFACE,

                 attempt + 1,

                 AT_PORT_OPEN_RETRY_COUNT,

                 esp_err_to_name(err));

        vTaskDelay(pdMS_TO_TICKS(AT_PORT_OPEN_RETRY_DELAY_MS));

    }

    return err;

}

static void close_at_port(void)

{

    if (s_at_port != NULL) {

        (void)usbh_cdc_port_close(s_at_port);

        s_at_port = NULL;

    }

}


/* ================================================================
 * SECTION: ECM Driver Lifecycle (create, destroy, wait, event handlers)
 * ================================================================ */

static void clear_runtime_events(void)

{

    if (s_runtime_events != NULL) {

        xEventGroupClearBits(s_runtime_events, ECM_RUNTIME_LINK_UP | ECM_RUNTIME_IP_UP);

    }

}

static void clear_request_events(void)

{

    if (s_request_events != NULL) {

        xEventGroupClearBits(s_request_events, ECM_REQ_RECONNECT | ECM_REQ_RESTART);

    }

}

static void destroy_ecm_stack(void)

{

    s_ecm_started = false;

    s_ip_acquired = false;

    clear_runtime_events();

    if (s_eth_handle != NULL) {
        (void)iot_eth_update_input_path(s_eth_handle, NULL, NULL);
    }

    if (s_netif_glue != NULL) {
        (void)iot_eth_del_netif_glue(s_netif_glue);
        s_netif_glue = NULL;
    }

    if (s_eth_handle != NULL) {

        (void)iot_eth_stop(s_eth_handle);

        (void)iot_eth_uninstall(s_eth_handle);

        s_eth_handle = NULL;

    }

    s_ecm_driver = NULL;

    if (s_ecm_netif != NULL) {

        esp_netif_destroy(s_ecm_netif);

        s_ecm_netif = NULL;

    }

}

static esp_err_t create_ecm_stack(void)

{

    esp_err_t err;

    iot_usbh_ecm_config_t ecm_cfg = {

        .match_id_list = s_ecm_match_ids,

    };

    err = iot_eth_new_usb_ecm(&ecm_cfg, &s_ecm_driver);

    if (err != ESP_OK) {

        return err;

    }

    iot_eth_config_t eth_cfg = {

        .driver = s_ecm_driver,

        .stack_input = NULL,

    };

    err = iot_eth_install(&eth_cfg, &s_eth_handle);

    if (err != ESP_OK) {

        s_ecm_driver = NULL;

        return err;

    }

    esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();

    base_cfg.if_key = "ECM_DEF";

    base_cfg.if_desc = "ec200a_ecm";

    esp_netif_config_t netif_cfg = {

        .base = &base_cfg,

        .driver = NULL,

        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,

    };

    s_ecm_netif = esp_netif_new(&netif_cfg);
    if (s_ecm_netif == NULL) {
        err = ESP_ERR_NO_MEM;
        goto err_cleanup;
    }

    s_netif_glue = iot_eth_new_netif_glue(s_eth_handle);
    if (s_netif_glue == NULL) {
        err = ESP_ERR_NO_MEM;
        goto err_cleanup;
    }

    err = esp_netif_attach(s_ecm_netif, s_netif_glue);
    if (err != ESP_OK) {
        goto err_cleanup;
    }

    err = iot_eth_start(s_eth_handle);
    if (err != ESP_OK) {
        goto err_cleanup;
    }

    s_ecm_started = true;

    return ESP_OK;

err_cleanup:
    destroy_ecm_stack();
    return err;

}

static esp_err_t wait_for_ecm_usb_ready(uint32_t timeout_ms)

{

    int64_t deadline_us = esp_timer_get_time() + ((int64_t)timeout_ms * 1000);

    while (esp_timer_get_time() < deadline_us) {

        if (s_usb_dev_addr != 0) {

            return ESP_OK;

        }

        vTaskDelay(pdMS_TO_TICKS(ECM_READY_POLL_MS));

    }

    return ESP_ERR_TIMEOUT;

}

static esp_err_t wait_for_ecm_ip(uint32_t timeout_ms)
{
    if (s_runtime_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    EventBits_t bits = xEventGroupWaitBits(s_runtime_events,
                                           ECM_RUNTIME_IP_UP,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & ECM_RUNTIME_IP_UP) != 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void cellular_ecm_usb_event_cb(usbh_cdc_device_event_t event,

                                      usbh_cdc_device_event_data_t *event_data,

                                      void *user_ctx)

{

    (void)user_ctx;

    if (event == CDC_HOST_DEVICE_EVENT_CONNECTED && event_data != NULL) {

        ESP_LOGI(TAG,

                 "USB CDC device connected: addr=%u matched_intf_num=%d vid=0x%04X pid=0x%04X",

                 event_data->new_dev.dev_addr,

                 event_data->new_dev.matched_intf_num,

                 event_data->new_dev.device_desc != NULL ? event_data->new_dev.device_desc->idVendor : 0,

                 event_data->new_dev.device_desc != NULL ? event_data->new_dev.device_desc->idProduct : 0);

        s_usb_dev_addr = event_data->new_dev.dev_addr;

        status_set_usb_connected(true);

        status_set_dial_status("EC200A detected, waiting for ECM/AT");

        status_set_last_error("");

    } else if (event == CDC_HOST_DEVICE_EVENT_DISCONNECTED && event_data != NULL) {

        ESP_LOGW(TAG, "USB CDC device disconnected: addr=%u", event_data->dev_gone.dev_addr);
        fault_log_record(FAULT_LOG_LEVEL_ERROR,
                         "usb",
                         "disconnect",
                         "addr=%u",
                         event_data->dev_gone.dev_addr);

        if (s_usb_dev_addr == event_data->dev_gone.dev_addr) {

            s_usb_dev_addr = 0;

        }

        clear_runtime_events();

        status_set_usb_connected(false);

        status_set_at_ready(false);

        status_set_uplink_connected(false);

        status_set_dial_status("EC200A USB disconnected");

        status_set_last_error("EC200A USB device disconnected.");

        if (s_request_events != NULL) {

            xEventGroupSetBits(s_request_events, ECM_REQ_RESTART);

        }

    }

}

static void cellular_ecm_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)

{

    (void)arg;

    if (event_base == IOT_ETH_EVENT) {

        switch (event_id) {

        case IOT_ETH_EVENT_START:

            status_set_dial_status("ECM starting");
            status_set_last_error("");

            ESP_LOGI(TAG, "IOT_ETH_EVENT_START");

            break;

        case IOT_ETH_EVENT_CONNECTED:

            if (s_runtime_events != NULL) {

                xEventGroupSetBits(s_runtime_events, ECM_RUNTIME_LINK_UP);

            }

            status_set_uplink_connected(true);

            status_set_reconnect_pending(false);

            status_set_dial_status("ECM link up");
            status_set_last_error("");

            ESP_LOGI(TAG, "IOT_ETH_EVENT_CONNECTED");

            break;

        case IOT_ETH_EVENT_DISCONNECTED:

            clear_runtime_events();

            status_set_uplink_connected(false);

            s_ip_acquired = false;

            status_set_dial_status("ECM link down");

            status_set_last_error("ECM uplink disconnected.");

            ESP_LOGW(TAG, "IOT_ETH_EVENT_DISCONNECTED");
            fault_log_record(FAULT_LOG_LEVEL_WARN, "ecm", "link_down", "IOT_ETH_EVENT_DISCONNECTED");

            if (!s_runtime_stopping && s_request_events != NULL) {

                xEventGroupSetBits(s_request_events, ECM_REQ_RECONNECT);

            }

            break;

        case IOT_ETH_EVENT_STOP:

            clear_runtime_events();

            status_set_uplink_connected(false);

            s_ip_acquired = false;

            status_set_dial_status("ECM stopped");
            ESP_LOGI(TAG, "IOT_ETH_EVENT_STOP");

            break;

        default:

            ESP_LOGI(TAG, "IOT_ETH_EVENT id=%" PRId32, event_id);

            break;

        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        char ip[16] = { 0 };

        char dns[16] = { 0 };

        if (event != NULL) {

            esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));

        }

        esp_netif_dns_info_t dns_info = { 0 };

        if (s_ecm_netif != NULL && esp_netif_get_dns_info(s_ecm_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {

            esp_ip4addr_ntoa(&dns_info.ip.u_addr.ip4, dns, sizeof(dns));

        }

        s_ip_acquired = true;

        if (s_runtime_events != NULL) {

            xEventGroupSetBits(s_runtime_events, ECM_RUNTIME_LINK_UP | ECM_RUNTIME_IP_UP);

        }

        status_set_ip_dns(ip, dns);

        status_set_uplink_connected(true);

        status_set_dial_status("ECM got IP");

        status_set_last_error("");

        ESP_LOGI(TAG, "IP_EVENT_ETH_GOT_IP: ip=%s dns=%s", ip[0] != '\0' ? ip : "-", dns[0] != '\0' ? dns : "-");
        fault_log_record(FAULT_LOG_LEVEL_INFO,
                         "ecm",
                         "ip_up",
                         "ip=%s dns=%s",
                         ip[0] != '\0' ? ip : "-",
                         dns[0] != '\0' ? dns : "-");

        if (event != NULL && event->esp_netif != NULL) {

            esp_netif_set_default_netif(event->esp_netif);

        } else if (s_ecm_netif != NULL) {

            esp_netif_set_default_netif(s_ecm_netif);

        }

        (void)enable_softap_napt();

    }

}

static esp_err_t cellular_ecm_install_cdc_driver(void)

{

    if (s_cdc_driver_ready) {

        return ESP_OK;

    }

    usbh_cdc_driver_config_t config = {

        .task_stack_size = 4096,

        .task_priority = APP_TASK_PRIO_USB_CDC,

        .task_coreid = APP_CORE_NETWORK,

        .skip_init_usb_host_driver = false,

    };

    ESP_RETURN_ON_ERROR(usbh_cdc_driver_install(&config), TAG, "Failed to install USBH CDC driver");

    static const usb_device_match_id_t dev_match_id[] = {

        {

            .match_flags = USB_DEVICE_ID_MATCH_VENDOR | USB_DEVICE_ID_MATCH_PRODUCT,

            .idVendor = EC200A_USB_VID,

            .idProduct = EC200A_USB_PID,

        },

        { 0 },

    };

    ESP_RETURN_ON_ERROR(usbh_cdc_register_dev_event_cb(dev_match_id, cellular_ecm_usb_event_cb, NULL),

                        TAG,

                        "Failed to register USB CDC device callback");

    s_cdc_driver_ready = true;

    ESP_LOGI(TAG, "USBH CDC driver installed for EC200A ECM probing");

    return ESP_OK;

}


/* ================================================================
 * SECTION: ECM State Machine (bringup, teardown, manager task)
 * ================================================================ */

static esp_err_t bringup_once(void)

{

    esp_err_t err;

    if (!s_ecm_started) {

        status_set_dial_status("Starting ECM stack");

        err = create_ecm_stack();

        if (err != ESP_OK) {

            status_set_last_error("Failed to create/start ECM stack.");

            return err;

        }

    }

    status_set_dial_status("Waiting for EC200A USB");

    err = wait_for_ecm_usb_ready(15000);

    if (err != ESP_OK) {

        status_set_last_error("EC200A USB enumerate timeout.");

        return err;

    }

    status_set_dial_status("Opening AT port");
    err = open_at_port();

    if (err != ESP_OK) {

        status_set_last_error("Failed to open EC200A AT interface.");

        return err;

    }

    status_set_dial_status("Handshaking AT");

    err = ensure_at_ready();

    if (err != ESP_OK) {

        status_set_last_error("AT handshake failed.");

        return err;

    }

    status_set_at_ready(true);

    status_set_dial_status("Configuring ECM mode");

    err = configure_modem_for_ecm();

    if (err != ESP_OK) {

        status_set_last_error("EC200A ECM/APN configuration failed.");

        return err;

    }

    update_radio_status_snapshot();

    status_set_dial_status("Waiting for ECM IP");

    err = wait_for_ecm_ip(ECM_IP_WAIT_TIMEOUT_MS);

    if (err != ESP_OK) {

        status_set_last_error("ECM IP acquire timeout.");

        return err;

    }

    return ESP_OK;

}

/* Keep the ECM/USB driver installed so it can observe fast USB re-enumeration. */
static void reset_session_state(void)

{

    s_runtime_stopping = true;

    close_at_port();

    clear_request_events();

    status_set_at_ready(false);

    status_set_uplink_connected(false);

    s_ip_acquired = false;

    s_runtime_stopping = false;

}

static void cellular_ecm_manager_task(void *arg)

{

    (void)arg;

    while (true) {

        esp_err_t err = bringup_once();

        if (err != ESP_OK) {
            char failure_reason[sizeof(s_status.last_error)];

            ESP_LOGW(TAG, "ECM bring-up failed: %s", esp_err_to_name(err));
            status_get_last_error(failure_reason, sizeof(failure_reason));
            persistent_diag_record_reconnect(failure_reason);
            s_consecutive_bringup_failures++;

            reset_session_state();

            status_set_dial_status("ECM bring-up failed");
            status_set_reconnect_pending(true);

            if (s_consecutive_bringup_failures >= ECM_FAILURES_BEFORE_HOST_RESTART) {
                ESP_LOGE(TAG,
                         "ECM recovery failed %u consecutive times, restarting ESP32 USB host",
                         s_consecutive_bringup_failures);
                fault_log_record(FAULT_LOG_LEVEL_ERROR,
                                 "ecm",
                                 "host_restart",
                                 "consecutive_failures=%u reason=%s",
                                 s_consecutive_bringup_failures,
                                 failure_reason[0] != '\0' ? failure_reason : "unknown");
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }

            vTaskDelay(pdMS_TO_TICKS(ECM_START_RETRY_DELAY_MS));

            continue;

        }

        status_set_reconnect_pending(false);
        s_consecutive_bringup_failures = 0;

        clear_request_events();

        status_set_last_error("");

        ESP_LOGI(TAG, "ECM minimal router path is up");

        while (true) {

            EventBits_t bits = xEventGroupWaitBits(s_request_events,

                                                   ECM_REQ_RECONNECT | ECM_REQ_RESTART,

                                                   pdTRUE,

                                                   pdFALSE,

                                                   pdMS_TO_TICKS(APP_AT_STABLE_POLL_MS));

            if ((bits & ECM_REQ_RESTART) != 0) {

                ESP_LOGW(TAG, "ECM restart requested");

                break;

            }

            if ((bits & ECM_REQ_RECONNECT) != 0) {

                ESP_LOGW(TAG, "ECM reconnect requested");

                break;

            }

            if (s_at_port != NULL) {

                update_radio_status_snapshot();

            }

        }

        char failure_reason[sizeof(s_status.last_error)];
        status_get_last_error(failure_reason, sizeof(failure_reason));
        persistent_diag_record_reconnect(failure_reason);
        status_set_reconnect_pending(true);

        status_set_dial_status("Rebuilding ECM link");

        reset_session_state();

        vTaskDelay(pdMS_TO_TICKS(ECM_RECONNECT_DELAY_MS));

    }

}


/* ================================================================
 * SECTION: Public API
 * ================================================================ */

esp_err_t cellular_ecm_start(esp_netif_t *ap_netif)

{

    BaseType_t task_ok;

    /* Create the mutex before any status_* call. */
    if (s_status_mutex == NULL) {
        s_status_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_status_mutex != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create status mutex");
    }

    s_ap_netif = ap_netif;

    app_config_set_defaults(&s_runtime_config);

    status_set_defaults();
    persistent_diag_load();

    if (s_request_events == NULL) {

        s_request_events = xEventGroupCreate();

        ESP_RETURN_ON_FALSE(s_request_events != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create ECM request event group");

    }

    if (s_runtime_events == NULL) {

        s_runtime_events = xEventGroupCreate();

        ESP_RETURN_ON_FALSE(s_runtime_events != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create ECM runtime event group");

    }

    ESP_RETURN_ON_ERROR(esp_event_handler_register(IOT_ETH_EVENT, ESP_EVENT_ANY_ID, &cellular_ecm_event_handler, NULL),

                        TAG,

                        "Failed to register IOT_ETH_EVENT handler");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &cellular_ecm_event_handler, NULL),

                        TAG,

                        "Failed to register ETH GOT IP handler");

    ESP_RETURN_ON_ERROR(cellular_ecm_install_cdc_driver(), TAG, "Failed to initialize USBH CDC base");

    if (s_manager_task != NULL) {

        return ESP_OK;

    }

    task_ok = xTaskCreatePinnedToCore(cellular_ecm_manager_task,

                                      "cellular_ecm",

                                      APP_TASK_STACK_CELLULAR_MANAGER,

                                      NULL,

                                      APP_TASK_PRIO_CELLULAR,

                                      &s_manager_task,

                                      APP_CORE_NETWORK);

    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "Failed to create ECM manager task");

    return ESP_OK;

}

void cellular_ecm_get_status(cellular_status_t *status)

{

    if (status == NULL) {

        return;

    }

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);

    *status = s_status;

    xSemaphoreGive(s_status_mutex);

}

esp_err_t cellular_ecm_request_reconnect(void)

{

    if (s_request_events == NULL) {

        return ESP_ERR_INVALID_STATE;

    }

    status_set_reconnect_pending(true);

    status_set_dial_status("ECM reconnect requested");

    status_set_last_error("");

    xEventGroupSetBits(s_request_events, ECM_REQ_RECONNECT);

    return ESP_OK;

}

esp_err_t cellular_ecm_apply_config(const app_config_t *config)

{

    if (config == NULL) {

        return ESP_ERR_INVALID_ARG;

    }

    s_runtime_config = *config;

    ESP_LOGI(TAG,

             "ECM config applied: ssid=%s channel=%u apn=%s",

             s_runtime_config.ssid,

             s_runtime_config.channel,

             app_config_get_effective_apn(&s_runtime_config));

    return ESP_OK;

}
