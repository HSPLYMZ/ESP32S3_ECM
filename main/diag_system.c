/* System self-diagnosis engine: 15 checks + internet ping + result cache. */

#include "diag_system.h"

#include "app_state.h"
#include "cellular_ecm.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fault_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "diag";

/* ---- Diagnosis item types ---- */
typedef enum {
    DIAG_OK = 0,
    DIAG_WARN = 1,
    DIAG_FAIL = 2,
} diag_status_t;

typedef struct {
    int id;
    const char *name;
    diag_status_t status;
    char detail[96];
    char suggest[96];
} diag_item_t;

/* ---- Cached result ---- */
#define DIAG_ITEM_COUNT 14
#define DIAG_CACHE_TTL_MS 30000
#define DIAG_INTERVAL_MS 30000
#define INTERNET_FAILURES_BEFORE_RECOVERY 3

static diag_item_t s_items[DIAG_ITEM_COUNT];
static int64_t s_last_run_us = 0;
static SemaphoreHandle_t s_mutex = NULL;
static uint8_t s_consecutive_internet_failures = 0;

/* ---- CSQ parser ---- */
static int parse_csq_rssi(const char *csq_str)
{
    int rssi = 99;
    if (csq_str && csq_str[0] && strcmp(csq_str, "--") != 0) {
        rssi = atoi(csq_str);
    }
    return rssi;
}

typedef struct {
    const char *name;
    uint32_t address;
    uint16_t port;
} internet_probe_target_t;

static const internet_probe_target_t s_internet_targets[] = {
    { "AliDNS", 0xDF050505, 53 },    /* 223.5.5.5 */
    { "BaiduDNS", 0xB44C4C4C, 53 }, /* 180.76.76.76 */
    { "114DNS", 0x72727272, 53 },    /* 114.114.114.114 */
};

static bool probe_tcp_target(const internet_probe_target_t *target)
{
    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }

    int flags = lwip_fcntl(sock, F_GETFL, 0);
    lwip_fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(target->port);
    addr.sin_addr.s_addr = htonl(target->address);

    int connect_result = lwip_connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (connect_result == 0) {
        lwip_close(sock);
        return true;
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    struct timeval tv = { .tv_sec = 1, .tv_usec = 500000 };

    int sel = lwip_select(sock + 1, NULL, &wfds, NULL, &tv);
    int socket_error = 0;
    socklen_t error_len = sizeof(socket_error);
    if (sel > 0 &&
        lwip_getsockopt(sock, SOL_SOCKET, SO_ERROR, &socket_error, &error_len) != 0) {
        socket_error = -1;
    }
    lwip_close(sock);

    return sel > 0 && socket_error == 0;
}

/* Any successful target is enough; one blocked public DNS must not report an outage. */
static bool check_internet_reachable(char *detail, size_t detail_len)
{
    for (size_t i = 0; i < sizeof(s_internet_targets) / sizeof(s_internet_targets[0]); ++i) {
        const internet_probe_target_t *target = &s_internet_targets[i];
        if (probe_tcp_target(target)) {
            snprintf(detail, detail_len, "互联网可达 (%s:%u 响应)", target->name, target->port);
            return true;
        }
    }

    snprintf(detail, detail_len, "全部 %u 个互联网探测目标均失败",
             (unsigned)(sizeof(s_internet_targets) / sizeof(s_internet_targets[0])));
    return false;
}

/* ---- Run all 14 checks ---- */
static void diag_run(void)
{
    app_state_snapshot_t state;
    cellular_status_t cell;
    int idx = 0;

    app_state_get_snapshot(&state);
    cellular_ecm_get_status(&cell);

    /* 1. USB */
    s_items[idx].id = 1;
    s_items[idx].name = "USB 连接";
    if (cell.usb_connected) {
        s_items[idx].status = DIAG_OK;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "EC200A 已识别");
    } else {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "未检测到 EC200A");
        snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "检查 EC200A 供电和 USB 连线");
    }
    idx++;

    /* 2. AT */
    s_items[idx].id = 2;
    s_items[idx].name = "AT 通信";
    if (cell.at_ready) {
        s_items[idx].status = DIAG_OK;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "AT 握手成功");
    } else if (cell.usb_connected) {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "AT 通信失败");
        snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "重启设备或检查 EC200A 固件");
    } else {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "等待 USB 连接");
    }
    idx++;

    /* 3. SIM */
    s_items[idx].id = 3;
    s_items[idx].name = "SIM 卡";
    if (strstr(cell.sim_status, "READY") != NULL) {
        s_items[idx].status = DIAG_OK;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "%s", cell.sim_status);
    } else if (cell.at_ready) {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "%s", cell.sim_status);
        snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "检查 SIM 卡是否正确插入");
    } else {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "等待 AT 就绪");
    }
    idx++;

    /* 4. Registration */
    s_items[idx].id = 4;
    s_items[idx].name = "网络注册";
    if (strstr(cell.cereg_status, ",1") != NULL || strstr(cell.cereg_status, ",5") != NULL) {
        s_items[idx].status = DIAG_OK;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "已注册 (%s)", cell.cereg_status);
    } else if (cell.at_ready) {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "%s", cell.cereg_status);
        snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "当前区域可能无 4G 信号覆盖");
    } else {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "等待 AT 就绪");
    }
    idx++;

    /* 5. Signal */
    {
        int csq = parse_csq_rssi(cell.signal_csq);
        s_items[idx].id = 5;
        s_items[idx].name = "信号强度";
        if (csq >= 14) {
            s_items[idx].status = DIAG_OK;
            snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "CSQ=%d, 良好", csq);
        } else if (csq >= 6) {
            s_items[idx].status = DIAG_WARN;
            snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "CSQ=%d, 偏弱", csq);
            snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "移动到信号更好的位置");
        } else if (csq >= 0) {
            s_items[idx].status = DIAG_FAIL;
            snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "CSQ=%d, 极弱", csq);
            snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "信号极弱，尝试移动设备位置");
        } else {
            s_items[idx].status = DIAG_FAIL;
            snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "--");
        }
    }
    idx++;

    /* 6. ECM link */
    s_items[idx].id = 6;
    s_items[idx].name = "ECM 链路";
    if (cell.uplink_connected) {
        s_items[idx].status = DIAG_OK;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "链路已建立");
    } else if (cell.reconnect_pending) {
        s_items[idx].status = DIAG_WARN;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "正在重连...");
        snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "等待系统自动重连，或点击重连按钮");
    } else {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "%s", cell.dial_status);
        snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "等待系统自动重连");
    }
    idx++;

    /* 7. IP */
    s_items[idx].id = 7;
    s_items[idx].name = "IP 获取";
    if (cell.uplink_ip[0] && strcmp(cell.uplink_ip, "--") != 0) {
        s_items[idx].status = DIAG_OK;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "IP=%s", cell.uplink_ip);
    } else if (cell.uplink_connected) {
        s_items[idx].status = DIAG_WARN;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "等待 DHCP 分配 IP");
    } else {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "ECM 链路未建立");
    }
    idx++;

    /* 8. DNS */
    s_items[idx].id = 8;
    s_items[idx].name = "DNS 可用";
    if (cell.dns[0] && strcmp(cell.dns, "--") != 0) {
        s_items[idx].status = DIAG_OK;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "上游 DNS=%s", cell.dns);
    } else {
        s_items[idx].status = DIAG_WARN;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "未获取 DNS");
        snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "APN 可能未分配 DNS");
    }
    idx++;

    /* 9. NAPT */
    s_items[idx].id = 9;
    s_items[idx].name = "NAPT 转发";
    if (cell.napt_enabled) {
        s_items[idx].status = DIAG_OK;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "已启用");
    } else {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "未启用");
        snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "系统内部错误，请重启设备");
    }
    idx++;

    /* 10. Internet */
    s_items[idx].id = 10;
    s_items[idx].name = "互联网连通";
    if (cell.uplink_ip[0] && strcmp(cell.uplink_ip, "--") != 0) {
        bool ok = check_internet_reachable(s_items[idx].detail, sizeof(s_items[idx].detail));
        s_items[idx].status = ok ? DIAG_OK : DIAG_FAIL;
        if (ok) {
            s_consecutive_internet_failures = 0;
        } else {
            s_consecutive_internet_failures++;
            if (s_consecutive_internet_failures == 1) {
                fault_log_record(FAULT_LOG_LEVEL_WARN,
                                 "diag",
                                 "internet_fail",
                                 "consecutive=%u ip=%s",
                                 s_consecutive_internet_failures,
                                 cell.uplink_ip[0] ? cell.uplink_ip : "-");
            }
            snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "检查 APN 配置和流量套餐");
            ESP_LOGW(TAG,
                     "Internet probe failure %u/%u",
                     s_consecutive_internet_failures,
                     INTERNET_FAILURES_BEFORE_RECOVERY);
            if (s_consecutive_internet_failures >= INTERNET_FAILURES_BEFORE_RECOVERY) {
                esp_err_t recovery_err = cellular_ecm_request_reconnect();
                ESP_LOGW(TAG, "Requesting ECM recovery after repeated internet failures: %s",
                         esp_err_to_name(recovery_err));
                fault_log_record(FAULT_LOG_LEVEL_ERROR,
                                 "diag",
                                 "internet_recover",
                                 "failures=%u reconnect=%s ip=%s",
                                 s_consecutive_internet_failures,
                                 esp_err_to_name(recovery_err),
                                 cell.uplink_ip[0] ? cell.uplink_ip : "-");
                s_consecutive_internet_failures = 0;
            }
        }
    } else {
        s_consecutive_internet_failures = 0;
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "无 ECM IP, 跳过检测");
    }
    ESP_LOGI(TAG, "Internet check: status=%d detail=%s", s_items[idx].status, s_items[idx].detail);
    idx++;

    /* 11. WiFi AP */
    s_items[idx].id = 11;
    s_items[idx].name = "Wi-Fi AP";
    if (state.softap_started) {
        s_items[idx].status = DIAG_OK;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "SSID=%s, 信道=%u", state.config.ssid, state.runtime_channel);
    } else {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "SoftAP 未启动");
        snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "WiFi 模块异常，请重启设备");
    }
    idx++;

    /* 12. DHCP */
    s_items[idx].id = 12;
    s_items[idx].name = "DHCP 服务";
    if (state.softap_started) {
        s_items[idx].status = DIAG_OK;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "%u 个客户端已连接", state.connected_sta_count);
    } else {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "SoftAP 未启动");
    }
    idx++;

    /* 13. Temperature */
    s_items[idx].id = 13;
    s_items[idx].name = "芯片温度";
    if (state.internal_temp_celsius < 55.0f) {
        s_items[idx].status = DIAG_OK;
    } else if (state.internal_temp_celsius < 65.0f) {
        s_items[idx].status = DIAG_WARN;
        snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "改善设备通风");
    } else {
        s_items[idx].status = DIAG_FAIL;
        snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "设备过热！请立即改善通风");
    }
    snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "%.1f °C", (double)state.internal_temp_celsius);
    idx++;

    /* 14. Memory */
    {
        size_t free_heap = esp_get_free_heap_size();
        s_items[idx].id = 14;
        s_items[idx].name = "系统内存";
        if (free_heap > 65536) {
            s_items[idx].status = DIAG_OK;
        } else if (free_heap > 32768) {
            s_items[idx].status = DIAG_WARN;
            snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "内存偏低，建议重启释放");
        } else {
            s_items[idx].status = DIAG_FAIL;
            snprintf(s_items[idx].suggest, sizeof(s_items[idx].suggest), "内存不足，请立即重启设备");
        }
        snprintf(s_items[idx].detail, sizeof(s_items[idx].detail), "空闲 %u KB", (unsigned)(free_heap / 1024));
    }

    s_last_run_us = esp_timer_get_time();
}

/* ---- Build JSON from cached result ---- */
int diag_system_get_json(char *buf, size_t buf_size)
{
    int pos = 0;
    int fail_count = 0, warn_count = 0, ok_count = 0;
    const char *score = "ok";
    const char *summary = "系统一切正常";

    if (buf == NULL || buf_size < 256 || s_mutex == NULL) return -1;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (int i = 0; i < DIAG_ITEM_COUNT; i++) {
        if (s_items[i].status == DIAG_FAIL) fail_count++;
        else if (s_items[i].status == DIAG_WARN) warn_count++;
        else ok_count++;
    }

    if (fail_count > 0) {
        score = "fail";
        /* Find first failure for summary */
        for (int i = 0; i < DIAG_ITEM_COUNT; i++) {
            if (s_items[i].status == DIAG_FAIL) {
                summary = s_items[i].detail;
                break;
            }
        }
    } else if (warn_count > 0) {
        score = "warn";
        summary = "有项目需要注意";
    }

    int64_t now = esp_timer_get_time();
    int age_sec = (int)((now - s_last_run_us) / 1000000);

    pos += snprintf(buf + pos, buf_size - pos,
        "{"
        "\"score\":\"%s\","
        "\"summary\":\"%s\","
        "\"ok\":%d,\"warn\":%d,\"fail\":%d,"
        "\"age\":%d,"
        "\"items\":[",
        score, summary, ok_count, warn_count, fail_count, age_sec);

    for (int i = 0; i < DIAG_ITEM_COUNT; i++) {
        const char *st = s_items[i].status == DIAG_OK ? "ok" :
                         s_items[i].status == DIAG_WARN ? "warn" : "fail";
        pos += snprintf(buf + pos, buf_size - pos,
            "%s{\"id\":%d,\"name\":\"%s\",\"status\":\"%s\",\"detail\":\"%s\"",
            (i > 0 ? "," : ""),
            s_items[i].id, s_items[i].name, st, s_items[i].detail);
        if (s_items[i].suggest[0]) {
            pos += snprintf(buf + pos, buf_size - pos,
                ",\"suggest\":\"%s\"", s_items[i].suggest);
        }
        pos += snprintf(buf + pos, buf_size - pos, "}");
        if (pos >= (int)buf_size - 64) break; /* safety truncation */
    }

    pos += snprintf(buf + pos, buf_size - pos, "]}");

    xSemaphoreGive(s_mutex);
    return pos;
}

/* ---- Background task: diagnose every 30s, recover only after repeated failures ---- */
static void diag_task(void *arg)
{
    (void)arg;
    /* Wait for system to stabilize on boot */
    vTaskDelay(pdMS_TO_TICKS(20000));

    while (true) {
        ESP_LOGI(TAG, "Running scheduled diagnosis...");
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        diag_run();
        xSemaphoreGive(s_mutex);
        ESP_LOGI(TAG, "Diagnosis complete");
        vTaskDelay(pdMS_TO_TICKS(DIAG_INTERVAL_MS));
    }
}

/* ---- Force immediate diagnosis ---- */
void diag_system_trigger(void)
{
    if (s_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    diag_run();
    xSemaphoreGive(s_mutex);
}

/* ---- Init ---- */
esp_err_t diag_system_init(void)
{
    if (s_mutex != NULL) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Run initial diagnosis */
    diag_run();

    BaseType_t ok = xTaskCreate(diag_task, "diag_sys", 4096, NULL, 3, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create diagnosis task");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Self-diagnosis system started");
    return ESP_OK;
}
