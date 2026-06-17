/* System self-diagnosis engine for V1.3.0 */

#pragma once

#include "esp_err.h"

esp_err_t diag_system_init(void);
void diag_system_trigger(void);
int diag_system_get_json(char *buf, size_t buf_size);
