# rt-carplay 应用

V2.1 的架构边界、生命周期修复、真实测试和独立实机限制见 [验证交接](docs/refactoring-v21-verification.md)。软件基线已验证，实机验收为 NOT_RUN。

Luban-Lite 侧的 rt-carplay 集成入口。CarPlay 协议栈与公共 Receiver Runtime 位于 SDK 内置的[`rt-carplay`](../../../packages/third-party/rt-carplay) git submodule；本目录拥有应用组合、连接管理、板级适配、Kconfig、构建集成及 `ui/lvgl_launcher.c` 启动页。首次构建前需要初始化 submodule：

```sh
git submodule update --init packages/third-party/rt-carplay
```

- 目标板：D213 `demo128-nand-carplay`，1024×600 面板
- 内核：RT-Thread
- 连接方式：USB 有线 CarPlay，或 Wi-Fi + 蓝牙无线 CarPlay
- 默认界面：LVGL USB / Wireless 启动页，USB 自动启动

## 目录内容

| 文件 | 说明 |
| --- | --- |
| `main.c` | 应用入口。默认等待 `carplay_start`，启用启动页或自动启动时按配置接管 |
| `Kconfig` | `RTCARPLAY_*` 应用配置项 |
| `SConscript` | 通过 `RTCARPLAY_DIR` 引入内置 rt-carplay 的 根目录正式 `SConscript`（不再编译示例应用） |
| `ui/lvgl_launcher.c` | 宿主拥有的 LVGL 启动页；连接 Worker 位于 app/connection_manager.c |
| `docs/` | 本应用的构建、验证与架构文档 |

## 内置组件与源码布局

SDK 通过 git submodule 固定 CarPlay 相关组件的版本，源码位于 SDK 目录内：

```text
luban-lite-carplay/
├── application/rt-thread/rtcarplay/       # 应用入口、Kconfig、构建集成
├── packages/third-party/rt-carplay/      # 协议栈、公共 Runtime、通用 Port（submodule）
├── packages/third-party/cherryusb-ipheth/ # Apple Ethernet / USB 类驱动（submodule）
├── packages/third-party/cherryusb-luban/ # CherryUSB 上游源码的 SDK 胶水
└── packages/third-party/cherryusb/        # 官方 CherryUSB 1.6.1 快照
```

- `rt-carplay`：路径由 `CONFIG_RTCARPLAY_DIR` 指定，默认 `../../../packages/third-party/rt-carplay`（相对本目录）。
- `cherryusb-ipheth`：由 `CONFIG_LPKG_CHERRYUSB_IPHETH_PATH` 指定，默认 `packages/third-party/cherryusb-ipheth`（相对 SDK 根目录）。
- `cherryusb-luban`：SDK 内置，见 [`packages/third-party/cherryusb-luban/README.md`](../../../packages/third-party/cherryusb-luban/README.md)。

submodule 的 URL 和固定 commit 记录在 SDK 仓库的 `.gitmodules` 与 gitlink 中：

```sh
git submodule status
git submodule update --init --recursive
```

需要临时对比或调试独立分支时，可在 menuconfig 中把上述路径改为 SDK 外的绝对路径，无需修改源码。

## 构建

当前正式配置为[`d21x_demo128-nand_rt-thread_rtcarplay_defconfig`](../../../target/configs/d21x_demo128-nand_rt-thread_rtcarplay_defconfig)。在 SDK 根目录执行：

```sh
scons --apply-def=d21x_demo128-nand_rt-thread_rtcarplay_defconfig
scons --info
scons -j8
```

全新 clone 在构建前还需要生成打包用的 `target/d21x/demo128-nand-carplay/pack/.image_cfg.json.tmp`。该文件正常由 bootloader 工程生成；当前 CherryUSB 1.6.1 缺少裸机 OSAL，bootloader 暂不可编译，而板级已提交 `pack/bootloader.bin`，因此手工生成地址文件后直接构建应用：

```sh
scons --apply-def=d21x_demo128-nand-carplay_baremetal_bootloader_defconfig
PYTHONPATH="$PWD/kernel/rt-thread/tools" python3 tools/scripts/calc_linked_addr.py \
  -i target/d21x/demo128-nand-carplay/pack/image_cfg.json -c .config \
  -o target/d21x/demo128-nand-carplay/pack/.image_cfg.json.tmp
scons --apply-def=d21x_demo128-nand_rt-thread_rtcarplay_defconfig
scons -j8
```

构建环境、全新构建顺序、镜像路径、烧录步骤和验证记录统一见 [`docs/build.md`](docs/build.md)。日常增量编译直接执行 `scons -j8`；不要每次重复 apply defconfig，否则会重置当前 `.config`。

## 运行

默认 defconfig 启用 `CONFIG_RTCARPLAY_DEMO_LAUNCHER=y`：启动后显示 LVGL选择页，USB 模式自动连接 iPhone，Wireless 模式启动 SoftAP、mDNS、蓝牙RFCOMM 和 iAP2 配对流程。

未启用启动页时，可选择：

- 启用 `CONFIG_RTCARPLAY_AUTO_START=y`：开机后自动启动接收器；
- 保持默认：在串口控制台手动执行：

```text
carplay_start
carplay_status
```

常用诊断命令：

```text
carplay_status
carplay_video status
carplay_bt status
carplay_iap2 status
ipheth_status
```

首次有线连接若需手动 bring-up，先关闭 `RTCARPLAY_DEMO_LAUNCHER`，再在控制台执行 `carplay_start`；配对 PIN 会打印在串口日志中。不要同时使用启动页和手动启停命令。完整验证步骤与已知问题见 [`docs/build.md`](docs/build.md)。

## 主要配置项

通过 `scons --menuconfig` 修改，位于 `rt-carplay application` 菜单。下表为当前defconfig 的实际取值：

| 配置项 | 当前值 | 说明 |
| --- | --- | --- |
| `RTCARPLAY_DEMO_LAUNCHER` | y | LVGL USB / Wireless 启动页 |
| `RTCARPLAY_AUTO_START` | n | 无启动页时开机自动启动 |
| `RTCARPLAY_DIR` | `../../../packages/third-party/rt-carplay` | 内置 rt-carplay submodule 路径 |
| `RTCARPLAY_USB_HOST` | y | CherryUSB 有线 CarPlay |
| `RTCARPLAY_BT` / `RTCARPLAY_IAP2` | y | 蓝牙 RFCOMM 与无线 iAP2 会话 |
| `RTCARPLAY_MFI` | y | MFi 2.0C 认证芯片 |
| `RTCARPLAY_VIDEO` | y（默认） | H.264 解码与 `aicfb` 显示 |
| `RTCARPLAY_AUDIO` | y | AirPlay 音频播放 |
| `RTCARPLAY_MICROPHONE` | y | Siri / 通话麦克风上行，需先确认 dmic0 硬件 |
| `RTCARPLAY_TOUCH_PANEL` | y | 触摸面板转发为 CarPlay HID |
| `RTCARPLAY_SCREEN_WIDTH/HEIGHT` | 1024 / 600 | 必须与面板一致 |

板级 pinmux、时钟和分区表位于[`target/d21x/demo128-nand-carplay/`](../../../target/d21x/demo128-nand-carplay/)。修改屏幕尺寸、触摸或音频通路时，需要同时核对板级配置与 defconfig。

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [`docs/build.md`](docs/build.md) | 仓库布局、首次构建、烧录、有线/无线验证、音频与已知问题 |
| [`docs/architecture.md`](docs/architecture.md) | rt-carplay 分层、目录设计、平台 seam 与接口边界 |

内置的 `packages/third-party/rt-carplay/docs/` 还包含更细的专题文档，例如`audio-subsystem.md`、`wireless-bringup.md`、`wired-video-performance.md`；协议实现和主机测试以该子模块的文档为准。

独立仓库、上游关系与完整 IMG CI：见 [CI 文档](docs/zh-CN/ci.md)（[English](docs/ci.md)）。
