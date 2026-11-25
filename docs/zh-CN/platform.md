[English](../platform.md) | [简体中文](platform.md)

# 架构与平台适配

## 所有权与数据流

`app/connection_manager.c` 仲裁传输方式。`app/carplay_app.c` 的轮询线程唯一拥有 Receiver，发布复制后的会话快照，并消费命令队列。Launcher 和 Fusion 渲染快照并提交用户请求。停止和切换流程见[会话快照](session-snapshots.md)和[连接管理](connection-manager.md)。

平台输出端在异步解码、显示和 VSync 期间保留媒体缓冲区所有权。适配时延续有界队列、背压和释放路径。复用 SDK 的平台描述结构，让音视频缓冲区继续沿现有路径流转。

## 依赖审计

| 范围 | 当前依赖 | 移植边界 |
| --- | --- | --- |
| Receiver 与命令 | 公共 `rtcp/receiver.h`、平台和媒体描述以及会话快照。应用组合和 iAP2 使用内部 `carplay_ctrl.h`、`iap2_session.h`、`iap2_stream.h` 及 AirPlay plist、发现辅助函数。 | 公共 Receiver API 提供等价契约后逐步迁移调用点。 |
| 有线与身份 | CherryUSB、USBMUX/lockdown 与 Host 钩子、原生 NCM 和 lwIP、I²C MFi、RT-Thread DFS、mbedTLS 及 AIC CE TRNG。Demo 使用内部 `lockdown.h` 和 `usbmux_host.h`。 | USB Ethernet 驱动留在 cherryusb-ipheth。USB、配对持久化、认证和随机数通过平台适配接入。 |
| 媒体与 UI | RT-Thread 设备、MPP/DE 与 framebuffer 图层命令、触摸、LVGL，以及内部混音、焦点、语音和抖动缓冲头文件。Host 测试也直接编译部分 SDK 内部源文件。 | 保留解码帧生命周期、异步 VSync 和音频回收。Host 白盒测试随 SDK 提交固定。 |
| 系统服务 | RT-Thread 线程与 DFS、音频/PWM/RTC、lwIP/AP 服务、CherryUSB ADB，以及 Luban-Lite A/B OTA 与 NAND 分区映射。 | 按服务接入现有应用与 SDK 的平台接口。Bootloader、分区布局和打包由 BSP 负责。 |

## 无线硬件变体

Realtek 基线在 `platform/rtcp_wifi_lubanlite.c` 使用 `wifi_conf.h` 和 `wifi_util.h`，在 `platform/wifi_sta_lubanlite.c` 使用 AP/STA 并发，蓝牙使用 Zephyr H:5、Classic BR/EDR、SDP、RFCOMM 和配对持久化。AIC8800D40 使用 SDK 的 SDIO WLAN 和蓝牙固件初始化，蓝牙传输采用 Zephyr H:4。RT-Thread SoftAP 启用前通过 SDK 设置 Apple Vendor IE。上板需验证启动顺序、Beacon 和 Probe Response 中的 Vendor IE、BR/EDR 配对及配对恢复，以及无线 CarPlay 重连。

AIC8800D40 在同一应用分支使用独立 defconfig、CI Matrix 项和固件目标标识。该配置关闭在线 OTA。为该模组开通更新通道前，在包元数据中写入兼容的目标标识，并验证不同无线变体之间的 OTA 会被拒绝。

## Linux 移植

从现有 CMake LVGL Preview 开始。复用 SDK 的 Linux OS 和网络适配，再接入 Receiver 所有者及命令、快照边界。逐个验证有线和无线身份、音频、视频与触摸适配。RT-Thread 继续使用 SCons/Kconfig，Linux 适配可用后逐步加入生产 CMake 目标。
