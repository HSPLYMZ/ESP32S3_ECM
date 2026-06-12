/*
 * ESP32S3_ECM_V1 任务放置定义
 * 当前版本：V1.2.3
 * 说明：当前最小路由版本将网络相关任务统一放在 Core 0。
 * V1.2.3 新增：显式栈大小定义、AT 稳定态轮询周期、PSRAM-aware 布局。
 */

#pragma once

#define APP_CORE_NETWORK      0
#define APP_CORE_BACKGROUND   1

/* 任务优先级 */
#define APP_TASK_PRIO_USB_CDC         19
#define APP_TASK_PRIO_CELLULAR        5
#define APP_TASK_PRIO_POWER_MANAGER   4

/* 任务栈大小（字节） */
#define APP_TASK_STACK_CELLULAR_MANAGER   8192
#define APP_TASK_STACK_POWER_MANAGER      4096

/* AT 稳定态轮询周期：链路稳定后每 5s 刷新一次信号/注册状态 */
#define APP_AT_STABLE_POLL_MS   5000