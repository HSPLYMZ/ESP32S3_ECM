# ESP32S3_ECM

基于 ESP32-S3 + EC200A-CN 的 4G 转 Wi-Fi 最小路由器，使用 ECM 上行、SoftAP 下发和 NAPT 转发。

## 项目信息

- 项目版本：V1.2.4
- 目标芯片：ESP32-S3
- 存储配置：16 MB Flash + 8 MB PSRAM
- 4G 模组：Quectel EC200A-CN
- 工作模式：ECM + SoftAP + NAPT

## V1.2.4 更新内容

- 修复 ECM suspend/teardown 顺序，在停用 ECM 前先解绑输入路径并释放 netif glue，避免空闲休眠阶段的崩溃重启。
- 修复 SoftAP DHCP DNS 下发选项，统一使用 DNS offer 标志，减少客户端连上热点后 DNS 异常的边缘问题。
- 修复 `cellular_ecm_start()` 中重复创建状态互斥锁的问题，并补齐 ECM 创建失败时的清理路径。
- 统一工程版本号到 `V1.2.4`，同步更新构建版本、README、版本记录和记忆文档。
- 清理 `main/` 下不必要的中文和乱码注释，将状态文案、错误信息和头文件说明收敛为简洁英文，代码更易维护。

## 功能概览

- EC200A ECM 上行：USB CDC 驱动安装、AT 握手、ECM 模式配置、APN 下发、IP 获取。
- Wi-Fi SoftAP：默认 SSID 为 `EC200A`，WPA2 加密，最多 2 个客户端。
- NAPT 转发：ECM 获取 IP 后自动启用 NAPT，并把上游 DNS 通过 SoftAP DHCP 下发给客户端。
- 热插拔重连：EC200A USB 断开或链路中断后自动进入重建流程。
- 状态采集：定期读取 SIM、信号、注册状态和网络制式。
- 温控与休眠：支持温度保护和空闲 light sleep。
- NVS 持久化：保存 SSID、密码、信道和 APN 配置。

## 构建与烧录

```bash
idf.py build
idf.py -p COM25 flash
```

## 文件结构

```text
main/
  main.c
  app_config.c/.h
  app_state.c/.h
  app_tasks.h
  cellular_ecm.c/.h
  wifi_ap.c/.h
  power_manager.c/.h
```

## 版本历史

| 版本 | 日期 | 说明 |
|---|---|---|
| V1.2.4 | 2026-06-12 | ECM suspend 崩溃修复、DNS 下发修复、代码与文档清理 |
| V1.2.3 | 2026-06-12 | OTA 分区准备、启动信息增强、状态互斥锁初始化修复 |
| V1.2.2 | 2026-06-12 | 状态快照栈占用优化、lwIP Core 1 绑定 |
| V1.2.1 | 2026-06-12 | 调度优化、PSRAM 启用、`-Os` 编译 |
| V1.2 | 2026-06-12 | ECM 最小闭环、温控保护、空闲休眠 |
| V1.1 | 2026-06-10 | ECM 最小路由基线 |
