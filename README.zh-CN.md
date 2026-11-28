[English](README.md) | [简体中文](README.zh-CN.md)

# rt-carplay-demo

本项目是 rt-carplay SDK 的 C 与 LVGL 参考应用，用于 Receiver 集成和验证。D213 支持 RT-Thread 和 Linux，提供 USB 与无线 CarPlay 及 1024×600 启动器。也可在 Linux PC 上构建和测试 Receiver。[SDK](https://github.com/boa-z/rt-carplay)

## 构建和连接

在 [Luban-Lite 集成仓库](https://github.com/boa-z/luban-lite-carplay) 中初始化固定版本的子模块，再构建 Bootloader、IMG 和 OTA CPIO。依赖和产物见[构建与 CI 指南](docs/zh-CN/ci.md#固件构建)。

```sh
git submodule update --init --recursive
bash .ci/build-firmware.sh
```

两种配置均适用于带 128 MiB SPI NAND 的 D213 `demo128-nand-carplay` 板。在集成仓库运行 `bash .ci/build-firmware.sh d213-demo128-nand-carplay-rtl8733bs` 或选用 `d213-demo128-nand-carplay-aic8800d40` 目标。每种目标生成对应的 IMG 和 A/B OTA CPIO。选定 defconfig 后可用 `scons -j8` 增量构建。刷机后可插入 USB 或通过蓝牙配对。[连接管理器](docs/zh-CN/connection-manager.md)管理唯一活动投屏会话，同时可保持另一种传输可用。关闭自动连接后需要单独调试时，参见[有线启动](docs/zh-CN/wired-startup.md)。

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
| 可移植 SDK | [rt-carplay](https://github.com/boa-z/rt-carplay) | RTSP、配对、加密媒体、iAP2、HID 和 Receiver 生命周期 |
| 应用层 | [`app/carplay_app.c`](app/carplay_app.c)、[`app/connection_manager.c`](app/connection_manager.c) | Receiver 单线程归属、传输仲裁、配对存储、会话快照和命令队列 |
| 板级适配 | [`platform/`](platform/) | 蓝牙、Wi-Fi、USB、MFi、MPP/DE、扬声器、麦克风和触摸 |
| 用户界面 | [`ui/lvgl_launcher.c`](ui/lvgl_launcher.c) | LVGL 设置和仪表、投屏显示及用户请求 |

连接管理器选择传输方式。应用的轮询线程拥有 Receiver 的调用权。媒体回调把数据交给板级线程，UI 读取复制后的快照并提交命令。USB 网络由独立维护的 `cherryusb-ipheth` 或原生 NCM 适配器提供。停止过程中的数据和资源归属见[会话快照](docs/zh-CN/session-snapshots.md)。

本地 `Kconfig` 和 `SConscript` 选择板级功能，SDK 协议开关由 SDK 的 `Kconfig` 管理。职责、内部依赖、Linux 和无线演进见[平台指南](docs/zh-CN/platform.md)。

## 使用和排查

| 任务 | 文档 |
| --- | --- |
| 正在播放、封面、导航、iPhone 状态与 RTC | [手机互联](docs/zh-CN/mobility-cockpit-v4.md#手机互联) |
| 系统导航、布局、主题及字体 | [Cockpit 设计系统](docs/zh-CN/design-system.md) |
| 全屏、融合、触摸映射和 30/60 FPS | [显示指南](docs/zh-CN/single-screen-fusion.md) |
| USB 枚举、信任或网络启动 | [有线启动](docs/zh-CN/wired-startup.md)和[原生 NCM](docs/zh-CN/wired-native-ncm.md) |
| 无线配对、重连和有线／无线交接 | [连接管理器](docs/zh-CN/connection-manager.md)和[蓝牙检查](docs/zh-CN/mobility-cockpit-v4.md#蓝牙名称) |
| MFi 芯片、软件认证身份与外部认证设备 | [认证配置](docs/zh-CN/platform.md#classic-配件认证) |
| 音频路由、Siri 和麦克风恢复 | [音频恢复](docs/zh-CN/audio-recovery.md) |
| AIC8800D40 和 RTL8733BS 热点及 Wi-Fi 维护 | [平台指南](docs/zh-CN/platform.md) |
| 网络、USB 文件及固件 | [网络管理器](docs/zh-CN/network-manager.md)、[网络共享](docs/zh-CN/internet-sharing.md)、[USB 文件](docs/zh-CN/usb-files.md)、[A/B OTA](docs/zh-CN/ota.md)和 [更新镜像](docs/zh-CN/ota-mirror.md) |

## 验证改动

```sh
export SDK_ROOT=/path/to/luban-lite-carplay
bash "$SDK_ROOT/packages/third-party/rt-carplay/scripts/fetch-mbedtls.sh"
make -j8 check
make check-docs
```

硬件矩阵和上板验收项见[验证指南](docs/zh-CN/validation.md)。依赖锁定与 Preview 见 [CI 指南](docs/zh-CN/ci.md#独立-host-验证)。

Demo 独立管理版本与 Git 标签。通过固件清单中的集成及依赖提交识别构建。公共 API、配置或用户可见行为变化时同步更新中英说明。授权和署名见 [NOTICE](NOTICE) 与 [COPYING](COPYING)。

## 调查记录

[媒体传输修复](docs/zh-CN/media-transport-repair.md)保留历次板端实验。[Ultra 开发](docs/zh-CN/ultra-development.md)和 [Vehicle State 测试台](docs/zh-CN/vehicle-protocol-demo.md)属于实验。默认 Classic 集成使用本地 LVGL 仪表。
