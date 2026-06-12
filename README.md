# ESP32S3_ECM_V1 (V1.2.1)

基于 ESP32-S3 + EC200A 的 4G 转 Wi-Fi 最小路由器，使用 ECM（Ethernet Control Model）模式上行。

## 项目信息

- 项目版本：V1.2.1
- 目标芯片：ESP32-S3 (QFN56, 240MHz, 8MB PSRAM)
- 4G 模组：Quectel EC200A-CN
- 工作模式：ECM (Ethernet Control Model) + SoftAP + NAPT

## V1.2.1 优化内容

比 V1.2 增加以下优化：

- FreeRTOS 调度优化：稳定态 AT 轮询从 500ms 降至 5s，减少 USB CDC 带宽占用和功耗
- Suspend 安全加固：用任务通知替代 busy-wait 循环，消除长期 suspend 时的 WDT 风险
- 启用 PSRAM：板载 8MB Octal PSRAM 加入堆分配器，释放内部 DRAM 给时敏路径
- 编译优化：Debug (-Og) 切换为 Release (-Os)，固件体积缩减约 20%
- 任务管理：显式定义栈大小宏（APP_TASK_STACK_CELLULAR_MANAGER / APP_TASK_STACK_POWER_MANAGER）

## 功能概览

- **EC200A ECM 上行**：USB CDC 驱动安装、AT 口初始化、ECM 模式配置、APN 下发、IP 获取、默认路由
- **Wi-Fi SoftAP**：802.11 b/g/n SoftAP，默认 SSID EC200A，WPA2 加密，最多 2 个 STA
- **NAPT 网络转发**：ECM IP 获取后自动启用 NAPT，DNS 同步到 SoftAP DHCP 下发，客户端透明上网
- **USB 热拔插与重连**：EC200A USB 插入/拔出自动重建链路
- **AT 状态快照**：SIM 状态、信号强度、注册状态、网络制式等信息采集（稳定态 5s 周期）
- **温控保护**：内部温度每 5s 采样，50C 预警，>=60C 持续 60s 触发 light sleep 保护，恢复阈值 55C
- **空闲休眠**：无客户端 + ECM 链路稳定 30s 后进入 light sleep，30s 定时唤醒并自动恢复
- **NVS 配置持久化**：SSID、密码、信道、APN 参数持久化存储

## 实测性能

- 下行速率 4.85 Mbps
- 上行速率 5.05 Mbps
- 时延 74 ms

## 硬件需求

- ESP32-S3（带 PSRAM）
- EC200A-CN 4G 模块（USB 连接）
- 4G SIM 卡

## 构建与烧录

```bash
idf.py build
idf.py -p COMx flash
```

## 文件结构

```
main/
  main.c              # 启动入口
  app_config.c/.h     # NVS 配置管理
  app_state.c/.h      # 线程安全共享状态
  app_tasks.h         # 任务优先级、栈大小、核心分配
  cellular_ecm.c/.h   # EC200A ECM 上行、AT、NAPT、重连
  wifi_ap.c/.h        # Wi-Fi SoftAP、DNS 下发
  power_manager.c/.h  # 温控与休眠管理
```

## 版本

V1.2.1 - 2026-06-12
- FreeRTOS 调度优化、PSRAM 启用、编译优化

V1.2 - 2026-06-12
- ECM 最小闭环稳定运行，NAPT 透明上网验证通过
- 新增温控保护与空闲休眠