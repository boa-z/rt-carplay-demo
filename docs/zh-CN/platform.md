[English](../platform.md) | [简体中文](platform.md)

# 架构与平台适配

## 所有权与数据流

两个平台共用 `app/receiver_session.c` 创建和销毁 Receiver 并注册回调，使用 `app/connection_retry.c` 计算有界重试延迟，使用 `app/phone_track.c` 合并 Now Playing 更新。各平台的 Receiver 所有者决定何时启动和结束会话，并负责按顺序释放借用的媒体资源。公共 Phone Hub 根据复制后的曲目信息显示播放状态和进度。

RT-Thread 由 `app/connection_manager.c` 仲裁传输方式。`app/carplay_app.c` 的轮询线程唯一拥有 Receiver，发布会话快照，并消费命令队列。Launcher 和 Fusion 渲染快照并提交用户请求。停止和切换流程见[会话快照](session-snapshots.md)和[连接管理](connection-manager.md)。

平台输出端在异步解码、显示和 VSync 期间保留媒体缓冲区所有权。适配时延续有界队列、背压和释放路径。复用 SDK 的平台描述结构，让音视频缓冲区继续沿现有路径流转。

## 依赖审计

| 范围 | 当前依赖 | 移植边界 |
| --- | --- | --- |
| Receiver 与命令 | 公共 `rtcp/receiver.h`、平台和媒体描述以及会话快照。应用组合和 iAP2 使用内部 `carplay_ctrl.h`、`iap2_session.h`、`iap2_stream.h` 及 AirPlay plist、发现辅助函数。 | 公共 Receiver API 提供等价契约后逐步迁移调用点。 |
| 有线与身份 | CherryUSB、USBMUX/lockdown 与 Host 钩子、原生 NCM 和 lwIP、I²C MFi、RT-Thread DFS、mbedTLS 及 AIC CE TRNG。Demo 使用内部 `lockdown.h` 和 `usbmux_host.h`。 | USB Ethernet 驱动留在 cherryusb-ipheth。USB、配对持久化、认证和随机数通过平台适配接入。 |
| 媒体与 UI | RT-Thread 设备、MPP/DE 与 framebuffer 图层命令、触摸、LVGL，以及内部混音、焦点、语音和抖动缓冲头文件。Host 测试也直接编译部分 SDK 内部源文件。 | 保留解码帧生命周期、异步 VSync 和音频回收。Host 白盒测试随 SDK 提交固定。 |
| 系统服务 | RT-Thread 线程与 DFS、音频/PWM/RTC、lwIP/AP 服务、CherryUSB ADB，以及 Luban-Lite A/B OTA 与 NAND 分区映射。 | 按服务接入现有应用与 SDK 的平台接口。Bootloader、分区布局和打包由 BSP 负责。 |

## 无线硬件变体

Realtek 在 `platform/rtcp_wifi_lubanlite.c` 使用 `wifi_conf.h` 和 `wifi_util.h`，蓝牙使用 Zephyr H:5、Classic BR/EDR、SDP、RFCOMM 和配对持久化。AIC8800D40 使用 SDK 的 SDIO WLAN 和蓝牙固件初始化，蓝牙传输采用 Zephyr H:4。RT-Thread SoftAP 启用前通过 SDK 设置 Apple Vendor IE。

两个目标的 OTA 固件都在**设置 → 连接 → Wi-Fi**提供 STA 维护模式，并显示 CarPlay 热点的实际状态。进入维护模式时暂停 CarPlay 连接管理，停止热点后启用 STA 扫描和连接。退出维护模式时断开 STA 并恢复原 CarPlay 连接策略。应用以互斥方式使用 AP 和 STA。AIC8800D40 上板需验证 Beacon 和 Probe Response 的 Vendor IE、蓝牙配对及配对恢复、STA 扫描及 WPA2 关联、DHCP 和 OTA 可达性，以及退出维护后无线 CarPlay 恢复。

AIC8800D40 是支持 A/B OTA 的独立硬件目标。其 CPIO 使用专用兼容 ID，RTL8733BS 保留旧 ID，两种目标均会在写 NAND 前拒绝另一目标的 CPIO。

## Linux 移植

Linux 主循环拥有 Receiver，并在同一线程同步处理 Launcher 命令。Launcher 读取包含 Receiver 和视频流代数的会话快照。切换端点时清理本地触点归属，避免把旧触点发送到新会话。libusb 和 ALSA 继续使用独立工作线程。Linux 在 USB 端点地址就绪时优先选择 USB，RT-Thread 通过连接管理线程预留传输方式。

本机可使用 `cmake -S . -B build/linux -DRTCP_ROOT=/path/to/rt-carplay -DRTCP_MBEDTLS_DIR=/path/to/mbedtls` 和 `cmake --build build/linux` 构建 Linux LAN Receiver。`CARPLAY_USB`、`CARPLAY_BLUEZ`、`CARPLAY_ALSA`、`CARPLAY_LVGL` 和 `CARPLAY_AIC_MPP` 独立启用。使用 LVGL 时需传入 `-DLVGL_ROOT=/path/to/lvgl`，版本见 `.ci/dependencies.env`。Luban 的 D21x Linux 固件启用全部五项。Linux 的 `theme`、`display-mode` 和 RT-Thread 的 `CPUI` 继续使用现有文件格式。

两个生产 Launcher 入口仍负责组装各自页面与绑定平台服务。外壳、Dashboard、Phone Hub 和 Fusion 仪表使用公共控件。RT-Thread Preview 使用其生产 Launcher 和这些公共控件。
