[English](README.md) | [简体中文](README.zh-CN.md)

# D213 CarPlay 应用

本应用将独立可移植 rt-carplay SDK 集成到 D213 的 Luban-Lite，使用 RT-Thread 和 1024×600 面板。[V2.1 验证](docs/zh-CN/refactoring-v21-verification.md)记录已完成的软件架构、生命周期基线。[Launcher V2](docs/zh-CN/launcher-v2.md)增加 USB/无线自动发现、GUI/Headless 共用的单会话 Owner 和 LVGL 9 仪表盘。当前源码和镜像的实机验收仍为 NOT_RUN。

## 源码所有权

| 目录 | 职责 |
| --- | --- |
| `app/` | Receiver 组合、自动连接管理器、USB/iAP2、配对与信任记录 |
| `platform/` | 板级 Wi-Fi/Bluetooth/MFi/USB、视频、音频/采集和触摸 |
| `ui/` | LVGL 9 Launcher V2 和宿主显示策略 |
| `tests/` | 实际源码协议/Worker/设备边界测试和实际 LVGL 预览 |
| `Kconfig` 和 `SConscript` | 应用配置和 SDK 正式构建集成 |

可移植 SDK 继续作为 `packages/third-party/rt-carplay` Git 子模块。其根构建入口提供通用协议、Runtime 和 Port，正式构建不编译 SDK 板级示例。独立 Apple Ethernet 支持保留在 `packages/third-party/cherryusb-ipheth`。CherryUSB Overlay 拥有 SDK/裸机集成，`packages/third-party/cherryusb` 保留官方 1.6.1 快照。子模块 URL 和准确版本记录在 `.gitmodules` 和 Gitlink。

## 构建

使用现有 `carplay-oss` 分支，按 [CI](docs/zh-CN/ci.md) 配置 Linux 工具链与依赖。先初始化全部子模块：

```sh
git submodule update --init --recursive
```

持续维护的构建脚本先编译同源码树裸机 Bootloader，核对打包二进制，再应用应用 Defconfig、构建并校验完整 IMG。裸机 CherryUSB OSAL 已实现并测试，不需要手工生成地址文件或使用未核对的预编译 Bootloader。

```sh
bash .ci/build-firmware.sh
```

从仓库根目录手动本地构建：

```sh
scons --apply-def=d21x_demo128-nand-carplay_baremetal_bootloader_defconfig
scons -j8
scons --apply-def=d21x_demo128-nand_rt-thread_rtcarplay_defconfig
scons --info
scons -j8
```

选择方案后，日常增量构建使用 `scons -j8`，反复应用 Defconfig 会重置本地配置。镜像位于 `output/{profile}/images/`。CI 将完整 IMG、A/B OTA 包和匹配的 Bootloader 作为滚动 `preview` GitHub Release 发布，并附带 SHA256 校验。构建不证明烧录或物理 CarPlay 运行。

## 运行

GUI Defconfig 选中 `RTCARPLAY_DEMO_LAUNCHER`，后者选中 `RTCARPLAY_AUTO_CONNECT`。USB 枚举在 Host 初始化前启用，应用启动无线发现并仲裁单个活动会话，无需选择模式。

自动构建使用管理器排队控制：

```text
carplay_connection status
carplay_connection retry
carplay_connection wireless off
carplay_connection wireless on
carplay_status
carplay_video status
carplay_bt status
carplay_iap2 status
ipheth_status
```

设置保存在 RAM，重启复位。关闭无线后 USB 仍启用。配对和信任记录独立持久化。手动调试前禁用两个自动选项，再使用 `carplay_start wired` 或 `carplay_start`，不要与活动管理器混用手动生命周期命令。

Settings → FIRMWARE & UPDATES 显示当前版本、源码提交和 A/B 槽位。检查更新通过有线以太网读取包含版本、完整提交、板型、包大小及 SHA256 的严格清单。不同提交会作为预览版本提供，其中也可能包含较旧提交。安装需要确认该确切目标并断开 CarPlay；工作线程将完整包暂存到 `/data`，校验存储内容后写入非活动槽位并重启。控制台 `carplay_ota status`、`carplay_ota check`、`carplay_ota install` 共用同一个任务入口。详见 [OTA 更新策略与验证](docs/zh-CN/ota.md)。

## 应用配置

`rt-carplay application` 菜单拥有板级选项，通用 SDK 选项在 SDK Kconfig 中有唯一 Owner。GUI 配置启用下列功能，Headless 则有意省略媒体和输入设备。

| 选项 | GUI 设置与用途 |
| --- | --- |
| `RTCARPLAY_DEMO_LAUNCHER` | LVGL 9 Launcher V2 |
| `RTCARPLAY_AUTO_CONNECT` | USB/无线自动单会话管理器 |
| `RTCARPLAY_AUTO_START` | 关闭，属于独立手动 Receiver 自动启动选项 |
| `RTCARPLAY_DIR` | `../../../packages/third-party/rt-carplay`，相对本目录 |
| `RTCARPLAY_USB_HOST` | CherryUSB 有线 CarPlay |
| `RTCARPLAY_BT` / `RTCARPLAY_IAP2` | Bluetooth RFCOMM 和无线 iAP2 |
| `RTCARPLAY_MFI` | 板级 MFi 2.0C Provider |
| `RTCARPLAY_VIDEO` | MPP H.264 和宿主持有的显示策略 |
| `RTCARPLAY_AUDIO` | sound0 输出，含媒体缓冲和共享时钟保护 |
| `RTCARPLAY_MICROPHONE` | dmic0 上行，需要装机声学验证 |
| `RTCARPLAY_MIC_AEC` | GUI 启用队列对齐的 AEC，支持运行时对比/校准 |
| `RTCARPLAY_TOUCH_PANEL` | 独占面板 Reader 和 CarPlay HID 路由 |
| `RTCARPLAY_SCREEN_WIDTH/HEIGHT` | 1024 / 600，与面板一致 |
| `RTCARPLAY_OTA` | Settings 版本显示与有线以太网 A/B 更新 |

Pinmux、时钟和分区布局归属[板级目录](../../../target/d21x/demo128-nand-carplay/)。修改媒体或输入时同时核对板级和应用配置。Launcher V2 保留了此前音频行为，后续[音频恢复](docs/zh-CN/audio-recovery.md)修改应用缓冲、时钟保护及 AEC 集成，不改变可移植 SDK 协议或编解码行为。

## 测试与文档

```sh
packages/third-party/rt-carplay/scripts/fetch-mbedtls.sh
make -C application/rt-thread/rtcarplay -j8 check
```

Host 测试在 ASan/UBSan 下运行实际源码，调度和设备边界使用模型。[Launcher 预览](docs/zh-CN/launcher-v2.md)进一步构建实际 LVGL 并执行 UI 事件。独立 SDK 和 IPHETH 仓库保留各自 Make/CMake/CTest 与双语文档检查。

| 文档 | 范围 |
| --- | --- |
| [有线启动与配对](docs/zh-CN/wired-startup.md) | CarKit 与网络同时启动、延迟邀请、RSA 优化及实机检查 |
| [EHCI 完成回收修复](docs/zh-CN/ehci-completion-recovery.md) | 已复现的异步推进回收缺陷、有线死机证据及验证边界 |
| [有线视频接收进度优化](docs/zh-CN/wired-video-recovery.md) | RX/TX 锁隔离、lwIP 排队输入及实机性能对比 |
| [mbedTLS 4.x 评估](docs/zh-CN/mbedtls-upgrade-assessment.md) | MPI 测量证据、迁移成本及性能边界 |
| [媒体与传输修复](docs/zh-CN/media-transport-repair.md) | 替换的音乐、回声与有线传输修改及验收边界 |
| [音频恢复](docs/zh-CN/audio-recovery.md) | 通话回声、音乐调度、运行时诊断和实机验收 |
| [Ethernet 互联网共享](docs/zh-CN/internet-sharing.md) | 可选 IPv4 网关、协议证据、恢复和必需的 iPhone 验收 |
| [V2 验证](docs/zh-CN/v2-verification.md) | 已验证版本、CI、IMG 校验值和剩余实机验收 |
| [连接管理器](docs/zh-CN/connection-manager.md) | 仲裁、重试、设置、所有权和 Headless 行为 |
| [Launcher V2](docs/zh-CN/launcher-v2.md) | 页面、真实 LVGL 预览和硬件清单 |
| [V2.1 验证](docs/zh-CN/refactoring-v21-verification.md) | 架构、迁移、生命周期、双语审计和 SDK 仓库清理 |
| [会话快照](docs/zh-CN/session-snapshots.md) | 并发、保留所有权、回调有效期和实际源码回归 |
| [CI](docs/zh-CN/ci.md) | 上游、工具链、构建打包和证据 |
| [历史调试](docs/build.md) | 早期调查和开发板反馈，不作为当前构建权威 |
| [可移植 SDK](../../../packages/third-party/rt-carplay/README.zh-CN.md) | 公共 API、Linux/RT-Thread Port、媒体和音频交接 |

开发使用现有 `carplay-oss` 和 SDK `main` 分支，采用 boa-z 身份，不创建 PR。旧 SDK 仓库按所有者要求备份并重建，旧 Gitlink、CI 引用需要经过验证的离线备份。当前子模块提交已发布且可获取。
