[English](README.md) | [简体中文](README.zh-CN.md)

# D213 CarPlay 集成

本应用在 ArtInChip D213 的 RT-Thread 上运行可移植的 rt-carplay SDK。默认固件使用 1024×600 的 LVGL 启动器，自动接受 USB 和无线 CarPlay。可移植 API 见 [SDK 接入指南](../../../packages/third-party/rt-carplay/docs/zh-CN/receiver-api.md)，功能状态及实机验收见 [Classic 能力矩阵](../../../packages/third-party/rt-carplay/docs/zh-CN/classic-capability-audit.md)。

## 构建和连接

在仓库根目录初始化指定版本的依赖，然后构建 Bootloader 和 IMG。Linux 依赖、输出文件名和产物见[构建与 CI 指南](docs/zh-CN/ci.md#固件构建)。

```sh
git submodule update --init --recursive
bash .ci/build-firmware.sh
```

默认配置为 `d21x_demo128-nand_rt-thread_rtcarplay_defconfig`，选定后运行 `scons -j8` 即可增量构建。镜像适用于带 128 MiB SPI NAND 的 D213 `demo128-nand-carplay` 板。刷机后可插入 USB 或通过蓝牙配对。[连接管理器](docs/zh-CN/connection-manager.md)管理唯一活动投屏会话，同时可保持另一种传输可用。关闭自动连接后需要单独调试时，参见[有线启动](docs/zh-CN/wired-startup.md)。

```text
carplay_connection status
carplay_status
carplay_video status
carplay_bt status
carplay_iap2 status
```

## 模块如何协作

| 层 | 代码入口 | 职责 |
| --- | --- | --- |
| 可移植 SDK | [rt-carplay](../../../packages/third-party/rt-carplay/README.zh-CN.md) | RTSP、配对、加密媒体、iAP2、HID 和 Receiver 生命周期 |
| 应用层 | [`app/carplay_app.c`](app/carplay_app.c)、[`app/connection_manager.c`](app/connection_manager.c) | Receiver 单线程归属、传输仲裁、配对存储、会话快照和命令队列 |
| 板级适配 | [`platform/`](platform/) | 蓝牙、Wi-Fi、USB、MFi、MPP/DE、扬声器、麦克风和触摸 |
| 用户界面 | [`ui/lvgl_launcher.c`](ui/lvgl_launcher.c) | LVGL 设置和仪表、投屏显示及用户请求 |

连接管理器选择传输方式。应用的轮询线程拥有 Receiver 的调用权。媒体回调把数据交给板级线程，UI 读取复制后的快照并提交命令。USB 网络由独立维护的 `cherryusb-ipheth` 或原生 NCM 适配器提供。停止过程中的数据和资源归属见[会话快照](docs/zh-CN/session-snapshots.md)。

本地 `Kconfig` 和 `SConscript` 选择板级功能，SDK 协议开关由 SDK 的 `Kconfig` 管理。设备驱动和 UI 留在宿主，能力声明要与实际接入的后端一致。

## 使用和排查

| 任务 | 文档 |
| --- | --- |
| 正在播放、封面、导航、iPhone 状态与 RTC | [手机控制](docs/zh-CN/mobility-cockpit-v4.md#手机控制) |
| 全屏、融合、触摸映射和 30/60 FPS | [显示指南](docs/zh-CN/single-screen-fusion.md) |
| USB 枚举、信任或网络启动 | [有线启动](docs/zh-CN/wired-startup.md)和[原生 NCM](docs/zh-CN/wired-native-ncm.md) |
| 无线配对、重连和有线／无线交接 | [连接管理器](docs/zh-CN/connection-manager.md)和[蓝牙检查](docs/zh-CN/mobility-cockpit-v4.md#蓝牙名称) |
| 音频路由、Siri 和麦克风恢复 | [音频恢复](docs/zh-CN/audio-recovery.md) |
| 有线以太网、USB 文件和固件更新 | [网络共享](docs/zh-CN/internet-sharing.md)、[USB 文件](docs/zh-CN/usb-files.md)和 [A/B OTA](docs/zh-CN/ota.md) |

## 验证改动

```sh
make -C packages/third-party/rt-carplay -j8 check
make -C application/rt-thread/rtcarplay -j8 check
make -C application/rt-thread/rtcarplay check-docs
```

SDK 回归验证协议和 API。集成回归使用生产源码和 Host 设备替身，CI 还渲染实际 LVGL 启动器和融合仪表条。D213 与 iPhone 的互操作需要实机验证，请按固件提交记录，并分别标明有线和无线结果。

公共 API、配置或用户可见行为变化时同步更新中英说明。能力状态统一更新到 SDK 矩阵，板端操作步骤记录在对应集成指南。

## 调查记录

[媒体传输修复](docs/zh-CN/media-transport-repair.md)保留历次板端实验。[Ultra 开发](docs/zh-CN/ultra-development.md)和 [Vehicle State 测试台](docs/zh-CN/vehicle-protocol-demo.md)属于实验。默认 Classic 集成使用本地 LVGL 仪表。
