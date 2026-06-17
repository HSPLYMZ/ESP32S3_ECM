/* Persistent local fault ring log stored in NVS.
 *
 * This intentionally records only sparse lifecycle/fault events instead of
 * periodic health samples, so it remains useful without wearing flash quickly.
 */

#include "fault_log.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "fault_log";

#define FAULT_LOG_NAMESPACE "fault_log"
#define FAULT_LOG_KEY_RING "ring"
#define FAULT_LOG_MAGIC 0x464C4F47U
#define FAULT_LOG_VERSION 1
#define FAULT_LOG_CAPACITY 32
#define FAULT_LOG_SOURCE_LEN 12
#define FAULT_LOG_EVENT_LEN 20
#define FAULT_LOG_DETAIL_LEN 80

typedef struct {
    uint32_t seq;
    uint32_t uptime_s;
    uint8_t level;
    char source[FAULT_LOG_SOURCE_LEN];
    char event[FAULT_LOG_EVENT_LEN];
    char detail[FAULT_LOG_DETAIL_LEN];
} fault_log_entry_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t capacity;
    uint32_t next_seq;
    uint32_t count;
    uint32_t head;
    fault_log_entry_t entries[FAULT_LOG_CAPACITY];
} fault_log_store_t;

static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;
static fault_log_store_t s_store;

static const char *level_to_text(uint8_t level)
{
    switch (level) {
    case FAULT_LOG_LEVEL_ERROR:
        return "error";
    case FAULT_LOG_LEVEL_WARN:
        return "warn";
    default:
        return "info";
    }
}

static void copy_clean(char *dst, size_t dst_len, const char *src)
{
    size_t used = 0;

    if (dst == NULL || dst_len == 0) {
        return;
    }

    while (src != NULL && src[0] != '\0' && used + 1 < dst_len) {
        unsigned char ch = (unsigned char)*src++;
        if (ch == '"' || ch == '\\') {
            dst[used++] = '\'';
        } else if (ch >= 0x20) {
            dst[used++] = (char)ch;
        } else {
            dst[used++] = ' ';
        }
    }
    dst[used] = '\0';
}

static void reset_store(void)
{
    memset(&s_store, 0, sizeof(s_store));
    s_store.magic = FAULT_LOG_MAGIC;
    s_store.version = FAULT_LOG_VERSION;
    s_store.capacity = FAULT_LOG_CAPACITY;
    s_store.next_seq = 1;
}

static bool store_is_valid(const fault_log_store_t *store, size_t blob_size)
{
    return store != NULL &&
           blob_size == sizeof(*store) &&
           store->magic == FAULT_LOG_MAGIC &&
           store->version == FAULT_LOG_VERSION &&
           store->capacity == FAULT_LOG_CAPACITY &&
           store->count <= FAULT_LOG_CAPACITY &&
           store->head < FAULT_LOG_CAPACITY &&
           store->next_seq != 0;
}

static esp_err_t persist_locked(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(FAULT_LOG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, FAULT_LOG_KEY_RING, &s_store, sizeof(s_store));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static bool appendf(char **cursor, size_t *remaining, const char *fmt, ...)
{
    va_list args;
    int written;

    if (cursor == NULL || *cursor == NULL || remaining == NULL || *remaining == 0) {
        return false;
    }

    va_start(args, fmt);
    written = vsnprintf(*cursor, *remaining, fmt, args);
    va_end(args);

    if (written < 0) {
        return false;
    }
    if ((size_t)written >= *remaining) {
        *cursor += *remaining - 1;
        *remaining = 1;
        return false;
    }

    *cursor += written;
    *remaining -= (size_t)written;
    return true;
}

esp_err_t fault_log_init(void)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_initialized) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    reset_store();

    err = nvs_open(FAULT_LOG_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        fault_log_store_t *loaded = (fault_log_store_t *)calloc(1, sizeof(*loaded));
        if (loaded == NULL) {
            nvs_close(handle);
            xSemaphoreGive(s_mutex);
            return ESP_ERR_NO_MEM;
        }

        size_t blob_size = sizeof(*loaded);
        err = nvs_get_blob(handle, FAULT_LOG_KEY_RING, loaded, &blob_size);
        nvs_close(handle);

        if (err == ESP_OK && store_is_valid(loaded, blob_size)) {
            s_store = *loaded;
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Ignoring invalid fault log store: %s", esp_err_to_name(err));
        }
        free(loaded);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        xSemaphoreGive(s_mutex);
        return err;
    }

    s_initialized = true;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Fault ring log ready: count=%" PRIu32 " next=%" PRIu32,
             s_store.count,
             s_store.next_seq);
    return ESP_OK;
}

void fault_log_record(fault_log_level_t level, const char *source, const char *event, const char *fmt, ...)
{
    fault_log_entry_t entry = { 0 };
    char formatted[128];
    esp_err_t err;

    if (!s_initialized || s_mutex == NULL) {
        return;
    }

    if (fmt != NULL) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(formatted, sizeof(formatted), fmt, args);
        va_end(args);
    } else {
        formatted[0] = '\0';
    }

    entry.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000LL);
    entry.level = (uint8_t)level;
    copy_clean(entry.source, sizeof(entry.source), source != NULL ? source : "system");
    copy_clean(entry.event, sizeof(entry.event), event != NULL ? event : "event");
    copy_clean(entry.detail, sizeof(entry.detail), formatted);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    uint32_t index = s_store.head % FAULT_LOG_CAPACITY;
    entry.seq = s_store.next_seq++;
    if (s_store.next_seq == 0) {
        s_store.next_seq = 1;
    }
    s_store.entries[index] = entry;
    s_store.head = (index + 1) % FAULT_LOG_CAPACITY;
    if (s_store.count < FAULT_LOG_CAPACITY) {
        s_store.count++;
    }

    err = persist_locked();
    xSemaphoreGive(s_mutex);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist fault log: %s", esp_err_to_name(err));
    }
}

int fault_log_get_json(char *buf, size_t buf_size)
{
    char *cursor = buf;
    size_t remaining = buf_size;
    bool ok = true;

    if (buf == NULL || buf_size == 0) {
        return -1;
    }

    if (!s_initialized || s_mutex == NULL) {
        int len = snprintf(buf, buf_size, "{\"error\":\"fault_log_not_ready\"}");
        return len > 0 ? len : -1;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    ok = appendf(&cursor, &remaining,
                 "{\"capacity\":%u,\"count\":%" PRIu32 ",\"next_seq\":%" PRIu32 ",\"entries\":[",
                 FAULT_LOG_CAPACITY,
                 s_store.count,
                 s_store.next_seq);

    uint32_t start = (s_store.count == FAULT_LOG_CAPACITY) ? s_store.head : 0;
    for (uint32_t i = 0; ok && i < s_store.count; ++i) {
        const fault_log_entry_t *entry = &s_store.entries[(start + i) % FAULT_LOG_CAPACITY];
        ok = appendf(&cursor, &remaining,
                     "%s{\"seq\":%" PRIu32 ",\"uptime_s\":%" PRIu32
                     ",\"level\":\"%s\",\"source\":\"%s\",\"event\":\"%s\",\"detail\":\"%s\"}",
                     i == 0 ? "" : ",",
                     entry->seq,
                     entry->uptime_s,
                     level_to_text(entry->level),
                     entry->source,
                     entry->event,
                     entry->detail);
    }

    if (ok) {
        ok = appendf(&cursor, &remaining, "]}");
    }

    xSemaphoreGive(s_mutex);

    if (!ok) {
        int len = snprintf(buf, buf_size, "{\"error\":\"fault_log_json_truncated\"}");
        return len > 0 ? len : -1;
    }

    return (int)(cursor - buf);
}
