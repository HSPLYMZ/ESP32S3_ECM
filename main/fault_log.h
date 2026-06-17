/* Persistent local fault ring log. */

#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef enum {
    FAULT_LOG_LEVEL_INFO = 0,
    FAULT_LOG_LEVEL_WARN = 1,
    FAULT_LOG_LEVEL_ERROR = 2,
} fault_log_level_t;

esp_err_t fault_log_init(void);
void fault_log_record(fault_log_level_t level, const char *source, const char *event, const char *fmt, ...);
int fault_log_get_json(char *buf, size_t buf_size);
