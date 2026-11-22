[English](README.md) | [简体中文](README.zh-CN.md)

# D213 CarPlay 应用

本应用将独立可移植 rt-carplay SDK 集成到 D213 的 Luban-Lite，使用 RT-Thread 和 1024×600 面板。[V2.1 验证](docs/zh-CN/refactoring-v21-verification.md)记录已完成的软件架构、生命周期基线。[Launcher V2](docs/zh-CN/launcher-v2.md)增加 USB/无线自动发现、GUI/Headless 共用的单会话 Owner 和 LVGL 9 仪表盘。当前源码和镜像的实机验收仍为 NOT_RUN。

## 源码所有权

[Ultra 开发](docs/ultra-development.zh-CN.md)记录 LIVI 源码研究、可移植 SDK 里程碑、统一 CI 产物及独立 L0-L5 验收。当前目标是可移植 Ultra 协议基础与实用的 D213 单屏融合 Demo，完整官方 Ultra 不属于当前验收范围，未实现能力保持不声明。用户已确认上一版本 Fusion 显示效果可以，具体受测镜像及剩余实机检查在开发报告中单独记录。Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI)。

统一 D213 配置可包含默认关闭的显式 [SIMULATED 发动机 Vehicle State 协议实验入口](docs/vehicle-protocol-demo.zh-CN.md)，用于后续手机互操作测试。它不会将本地仪表的车速、挡位、电量随意映射成协议特征，也不启用 UI Sync 或官方 Local UI 渲染。

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

设置 → 关于显示 Luban-Lite SDK 版本、rt-carplay SDK 版本、固件提交和 UTC 构建时间。设置 → 系统更新显示当前固件版本、源码提交和 A/B 槽位。检查更新通过有线以太网读取包含版本、完整提交、板型、包大小及 SHA256 的严格清单。不同提交会作为预览版本提供，其中也可能包含较旧提交。安装需要确认该确切目标并断开 CarPlay。工作线程将完整包暂存到 `/data`，校验存储内容后写入非活动槽位并重启。控制台 `carplay_ota status`、`carplay_ota check`、`carplay_ota install` 共用同一个任务入口。详见 [OTA 更新策略与验证](docs/zh-CN/ota.md)。

## Classic CarPlay 控制

在设置 → 手机控制中，可独立于 Launcher 主题请求 iPhone 使用自动、浅色或深色外观。本板自动模式使用日间外观。iPhone 上报播放信息后，页面显示曲名、艺人和播放状态。命令排队和套接字写入只反映接收端的进度，实际效果请查看 iPhone 渲染的 CarPlay 界面及地图。

无线模式下，Demo 同时接收 AirPlay 与 Wi-Fi 隧道内的 iAP2 播放信息。诊断时从连接前开始录串口，依次播放、切歌和暂停，再执行 `carplay_status`。保留含 `now_playing` 的行即可检查 `0x5000` 入队、`0x5001` 或 AirPlay 更新的解析，以及 Demo 缓存的字段状态。日志不含曲名或艺人文本。

无线 iAP2 隧道还会更新「设置 → 手机控制」中的「路线引导」卡片。在 iPhone 上开始导航，查看手机提供的目的地、当前道路和剩余距离，再结束路线和断开连接，确认卡片内容清除。`carplay_status` 的 `carplay route_guidance` 行记录各字段是否收到。

「正在播放」和「路线引导」在中英文界面均使用完整的 Noto Sans SC 字符映射。见[字库覆盖与生成方式](ui/fonts/README.md)。

### 模拟定位

未接 GPS 模块的 D213 可以打开「设置 → 模拟定位」。「固定点」使用 A 点。「A 点到 B 点」需要两个不同且非对跖的坐标，以及 1 至 300 km/h 的整数速度。经纬度使用带正负号的十进制度，最多六位小数。先选择「开启」再无线连接。若现有会话建立时模拟处于关闭状态，开启后需重新连接。收到手机定位请求后，模拟沿大圆路径从 A 出发，到 B 后继续以零速度发送 B 点。点「应用」后，下一次采样会更新固定点或从 A 重新出发。点「关闭」会停止提交新定位。配置仅保存在内存，重启后模拟默认关闭。

无线 iAP2 隧道在识别阶段声明 GGA/RMC 能力，收到请求后每秒为每个请求的类型发送一条。移动时 RMC 包含以节为单位的地速和相对于真北的航向。`carplay_gnss` 显示启用状态、能力声明、请求位掩码（`0x1` 为 GGA、`0x2` 为 RMC）、本次会话入队及失败计数、时间来源和移动状态。板载时钟未设置时，`clock=fixture` 表示使用合成的 2026 年 1 月 1 日 UTC 日期。验证时无线连接后打开地图，对照状态从 `moving` 变为 `arrived` 时地图上的位置。入队计数表示 SDK 接收了待发数据，iPhone 自行决定是否采用外部定位。

D213ECV-DEMO V4.0 的 K4–K7 共用 `PA.6` 上的 `KEYADC` 电阻梯形网络，见原理图第 2、6 页和硬件说明书 §2.5.2（印刷页码 9）。Demo defconfig 启用 `RTCARPLAY_D213_BOARD_KEYS` 和 GPAI 通道 6，使用复用功能 2，并关闭内部上下拉。`RESET`、`UBOOT` 和 `WAKEUP` 保留板级用途。CarPlay 会话活动时，短按实体键将下表动作加入发送队列。

| 板上按键 | 图纸标注电压 | Demo 功能 | RT-Thread 串口命令 |
| --- | --- | --- | --- |
| K4 UP | 0.3 V | CarPlay 返回 | `carplay_phone_key back` |
| K5 DOWN | 0.6 V | CarPlay 主页 | `carplay_phone_key home` |
| K6 LEFT | 1.0 V | Siri | `carplay_siri` |
| K7 RIGHT | 1.3 V | 播放或暂停 | `carplay_phone_key play-pause` |

空闲及逐键按住时运行 `carplay_board_keys`，可查看最近一次 ADC 计数和识别结果。初始窗口采用 BOM 阻值（10 kΩ 上拉，梯形电阻依次为 1、2.2、4.7、6.8 kΩ）、3.3 V 上拉电源及 SDK 的 3.0 V、12 位 GPAI 量程。图纸标注电压和分压计算有差异，需上板核对窗口。适配每 20 ms 采样，按下和释放均需连续三次读数确认，长按只触发一次。Receiver 工作线程发送手机控制命令。串口也支持 `carplay_phone_key previous` 和 `carplay_phone_key next`。融合画面的 Dashboard 入口返回本机 Launcher。

D213 与 iPhone 验收时，先用 USB 连接，逐个测试实体键，包括长按和释放后再按。用 `carplay_board_keys` 核对 ADC 识别，必要时执行上表串口命令单独验证手机侧行为。保持地图可见，切换手机深浅外观并检查本机 Launcher 主题。播放音乐时将本机音量调至静音再恢复，切歌后核对播放信息，分别触发导航、通话和 Siri，检查压低音量、恢复、麦克风收音和音质。播放中拔掉 USB，再让已配对的 iPhone 切换到无线连接，强制断开 Wi-Fi 后重复验证。用 `carplay_connection status` 查看连接管理器状态，确认断开后旧的播放信息清空。

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
| `RTCARPLAY_SCREEN_WIDTH/HEIGHT` | 回退逻辑尺寸 1024 / 600；连接管理器在融合模式请求 768 / 600，全屏模式请求 1024 / 600 |
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
| [有线原生 NCM 集成](docs/zh-CN/wired-native-ncm.md) | 独占网络所有权、模块归属、三款手机验收及回归范围 |
| [有线视频接收进度优化](docs/zh-CN/wired-video-recovery.md) | RX/TX 锁隔离、lwIP 排队输入及实机性能对比 |
| [mbedTLS 4.x 评估](docs/zh-CN/mbedtls-upgrade-assessment.md) | MPI 测量证据、迁移成本及性能边界 |
| [媒体与传输修复](docs/zh-CN/media-transport-repair.md) | 替换的音乐、回声与有线传输修改及验收边界 |
| [音频恢复](docs/zh-CN/audio-recovery.md) | 通话回声、音乐调度、运行时诊断和实机验收 |
| [Ethernet 互联网共享](docs/zh-CN/internet-sharing.md) | 可选 IPv4 网关、协议证据、恢复和必需的 iPhone 验收 |
| [V2 验证](docs/zh-CN/v2-verification.md) | 已验证版本、CI、IMG 校验值和剩余实机验收 |
| [连接管理器](docs/zh-CN/connection-manager.md) | 仲裁、重试、设置、所有权和 Headless 行为 |
| [Launcher V2](docs/zh-CN/launcher-v2.md) | 历史 V2 页面集与连接管理器行为；产品界面现为 V4 |
| [Mobility Cockpit V4](docs/zh-CN/mobility-cockpit-v4.md) | 四个原生页面、主题/语言、真实音量/亮度、投影模式和 OTA 绑定 |
| [V4 主机证据](docs/evidence/d213-mobility-cockpit-v4-20261116.zh-CN.md) | 当前源码/镜像状态、主机结果和剩余板端检查 |
| [V2.1 验证](docs/zh-CN/refactoring-v21-verification.md) | 架构、迁移、生命周期、双语审计和 SDK 仓库清理 |
| [会话快照](docs/zh-CN/session-snapshots.md) | 并发、保留所有权、回调有效期和实际源码回归 |
| [CI](docs/zh-CN/ci.md) | 上游、工具链、构建打包和证据 |
| [历史调试](docs/build.md) | 早期调查和开发板反馈，不作为当前构建权威 |
| [可移植 SDK](../../../packages/third-party/rt-carplay/README.zh-CN.md) | 公共 API、Linux/RT-Thread Port、媒体和音频交接 |

开发使用现有 `carplay-oss` 和 SDK `main` 分支，采用 boa-z 身份，不创建 PR。旧 SDK 仓库按所有者要求备份并重建，旧 Gitlink、CI 引用需要经过验证的离线备份。当前子模块提交已发布且可获取。
