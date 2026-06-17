# ESP32S3_ECM (V1.3.0)

基于 ESP32-S3 + Quectel EC200A-CN 的 4G 转 Wi-Fi 路由工程。系统通过 USB ECM 获取蜂窝上行，在 ESP32-S3 上提供 SoftAP 热点，并使用 NAPT 为客户端转发上网。

## 项目信息

- 固件版本：V1.3.0
- GitHub 仓库：https://github.com/HSPLYMZ/ESP32S3_ECM
- 目标芯片：ESP32-S3
- 存储配置：16 MB Flash + 8 MB PSRAM
- 4G 模组：Quectel EC200A-CN
- 工作模式：ECM + SoftAP + NAPT
- 默认热点：`EC200A`
- 常用串口：`COM25`

## V1.3.0 更新内容

- 清理 `cellular_ecm.c` 内已经废弃的休眠、挂起接口和大量不可达分支，减少 ECM 链路重建时的状态复杂度。
- 增强 ECM 自动恢复机制：保留 ECM 驱动栈，断链后优先重建上行；连续失败时记录原因并触发主机级恢复。
- 新增持久化联网诊断信息，保存重连次数、失败次数、最近失败原因和 CGATT 状态，方便定位偶发断网问题。
- 联网诊断改为多目标检测，使用 `223.5.5.5:53`、`180.76.76.76:53`、`114.114.114.114:53`，避免单一海外 DNS 被运营商屏蔽时误报断网。
- SoftAP DHCP 固定下发公共 DNS，移除不必要的 `APSTA -> AP` 模式切换，并关闭 Wi-Fi 省电以提升热点稳定性。
- 新增本地故障环形日志，关键故障写入 NVS，并通过 WebUI 的 `/api/faults` 接口读取。
- 提升 WebUI 可靠性：监听 socket 非阻塞、客户端读写超时、监控任务自动恢复卡住的 WebUI 服务。
- WebUI 更新为 iOS 风格控制中心界面，并在页脚加入作者邮箱和 GitHub 地址。
- WebUI、诊断、LED、电源管理等非核心功能启动失败时不再使用 `ESP_ERROR_CHECK` 直接导致整机重启。

## 功能概览

- EC200A ECM 上行：USB CDC 驱动安装、AT 握手、ECM 模式配置、APN 配置、蜂窝 IP 获取。
- Wi-Fi SoftAP：默认 SSID 为 `EC200A`，支持 WPA2 加密和客户端接入状态统计。
- NAPT 转发：ECM 获取 IP 后自动启用 NAPT，使热点客户端透明访问蜂窝网络。
- 自动恢复：USB 断开、ECM 链路中断、联网探测失败时进入分级恢复流程。
- 联网诊断：周期性检测多个公共 DNS 目标，连续失败后请求 ECM 重连。
- 本地故障日志：保留最近故障、启动、链路和 WebUI 异常记录，掉电后可继续排查。
- WebUI 控制台：提供状态、诊断、故障日志、配置查看和 iOS 风格响应式界面。
- NVS 持久化：保存热点配置、APN、ECM 诊断信息和故障环形日志。

## 构建与烧录

```powershell
idf.py build
idf.py -p COM25 flash
```

当前本地工程也可直接使用已配置的 Ninja 构建目录：

```powershell
C:\Espressif\tools\ninja\1.12.1\ninja.exe -C build
```

## 文件结构

```text
main/
  main.c                 启动编排与核心服务初始化
  app_config.c/.h        NVS 配置管理
  app_state.c/.h         全局运行状态
  app_tasks.h            FreeRTOS 任务优先级/核心绑定约定
  cellular_ecm.c/.h      EC200A ECM 上行与恢复流程
  wifi_ap.c/.h           SoftAP、DHCP、NAPT
  diag_system.c/.h       联网诊断与自动恢复触发
  fault_log.c/.h         本地故障环形日志
  webui.c/.h             WebUI 和 HTTP API
  power_manager.c/.h     温度/电源辅助管理
```

## 版本历史

| 版本 | 日期 | 说明 |
|---|---|---|
| V1.3.0 | 2026-06-17 | 断网诊断与恢复增强、本地故障日志、WebUI 可靠性和 iOS 风格界面 |
| V1.2.5 | 2026-06-17 | 移除休眠路径、增加温控 LED、AT 重试、CGATT 等待和 TWDT 看门狗 |
| V1.2.4 | 2026-06-12 | ECM suspend 崩溃修复、DNS 下发修复、代码与文档清理 |
| V1.2.3 | 2026-06-12 | OTA 分区准备、启动信息增强、状态互斥锁初始化修复 |
| V1.2.2 | 2026-06-12 | 状态快照栈占用优化、lwIP Core 1 绑定 |
| V1.2.1 | 2026-06-12 | 调度优化、PSRAM 启用、`-Os` 编译 |
| V1.2 | 2026-06-12 | ECM 最小闭环、温控保护、空闲休眠 |
| V1.1 | 2026-06-10 | ECM 最小路由基线 |
