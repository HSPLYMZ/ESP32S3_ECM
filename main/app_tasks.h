/* Task priorities, stack sizes, and core placement. */

#pragma once

#define APP_CORE_NETWORK 0
#define APP_CORE_BACKGROUND 1

/* Task priorities */
#define APP_TASK_PRIO_USB_CDC 19
#define APP_TASK_PRIO_CELLULAR 5
#define APP_TASK_PRIO_POWER_MANAGER 4

/* Task stack sizes in bytes */
#define APP_TASK_STACK_CELLULAR_MANAGER 8192
#define APP_TASK_STACK_POWER_MANAGER 4096

/* Stable-state AT poll interval */
#define APP_AT_STABLE_POLL_MS 5000
