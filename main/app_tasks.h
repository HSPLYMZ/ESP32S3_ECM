/*
 * ESP32S3_ECM_V1 任务放置定义
 * 当前版本：V1.2
 * 说明：当前最小路由版本将网络相关任务统一放在 Core 0。
 */

#pragma once

#define APP_CORE_NETWORK 0
#define APP_CORE_BACKGROUND 1
#define APP_TASK_PRIO_USB_CDC       19
#define APP_TASK_PRIO_CELLULAR      5
#define APP_TASK_PRIO_POWER_MANAGER 4
