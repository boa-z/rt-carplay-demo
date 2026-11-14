# D213 开源 CarPlay 开发与构建

> 历史调试记录：下文版本、旧路径和当时构建限制保留为历史证据。当前 V2 架构、自动连接、同源码树裸机 Bootloader 与完整 IMG 构建以 [应用 README](../README.zh-CN.md)、[CI](zh-CN/ci.md) 和 [Launcher V2](zh-CN/launcher-v2.md) 为准，历史运行结果不能作为当前镜像的实机验收。


> 应用概览、配置摘要和文档索引见 [README](../README.md)。

最新的 READY 主动寻呼、控制器扫描核验固件及验证方式见第 11 节。

本文包含无线 v30 基线及已合入的音频子系统。当前使用D213、1024×600 面板、RT-Thread，以及 USB／无线双模式 LVGL 启动页。用户已验证无线视频及 v30 蓝牙交接正常；本轮新增音频只有 Host 和 SDK 构建验证。视频接收已运行，持续动态画面的低帧率仍在定位；当前默认公告 30 FPS，编译成功不代表已达到 60 FPS。

## 1. 仓库布局与 submodule

SDK 通过 git submodule 固定 CarPlay 相关组件的版本，源码位于 SDK 目录内：

```text
luban-lite-carplay/
├── application/rt-thread/rtcarplay/        # 应用入口、Kconfig、构建集成
├── packages/third-party/rt-carplay/       # 协议、RT-Thread 应用、LVGL、USBMUX、VE/DE 适配
└── packages/third-party/cherryusb-ipheth/  # Apple Ethernet 协议、USB 类驱动、lwIP 适配
```

| 仓库 | 远程地址 | SDK 中的路径 |
| --- | --- | --- |
| SDK | https://github.com/boa-z/luban-lite-carplay.git | 本仓库 |
| CarPlay | https://github.com/boa-z/rt-carplay.git | `packages/third-party/rt-carplay` |
| IPHETH | https://github.com/boa-z/cherryusb-ipheth.git | `packages/third-party/cherryusb-ipheth` |

首次获取源码时初始化 submodule：

```sh
git clone --branch carplay-oss https://github.com/boa-z/luban-lite-carplay.git luban-lite-carplay
cd luban-lite-carplay
git submodule update --init --recursive
```

submodule 的 URL 和固定 commit 由 `.gitmodules` 与 gitlink 管理，`git submodule status` 可查看当前固定的 commit。更新组件时在对应 submodule内切换到目标 commit，回到 SDK 根目录提交 gitlink；不要只在子模块内修改文件而忘记更新 SDK 侧指针。

## 2. 实际源码如何进入固件

* 应用入口：`application/rt-thread/rtcarplay/main.c`。
* 应用构建：同目录 `SConscript` 根据 `CONFIG_RTCARPLAY_DIR` 引用内置
  `packages/third-party/rt-carplay/SConscript`，直接编译 submodule 源码。
* IPHETH 构建：`packages/third-party/cherryusb-ipheth-luban/SConscript` 根据
  `CONFIG_LPKG_CHERRYUSB_IPHETH_PATH` 引用 submodule 的`integration/SConscript`；该 package 只保留 SDK 胶水。
* 内核与网络：本 SDK 的 `kernel/rt-thread` 和配置选中的 lwIP。
* USB：`packages/third-party/cherryusb` 保存官方 CherryUSB 1.6.1 快照
  `a0054795c44037ace8a2b77b6e610adbff2257ea`。SDK 适配在`packages/third-party/cherryusb-luban`；SCons 将补丁应用到 `generated/` 副本，实际编译该副本。修改应进入补丁或适配源码，不直接编辑生成文件。

默认 defconfig 的关键路径：

```text
CONFIG_RTCARPLAY_DIR="../../../packages/third-party/rt-carplay"
CONFIG_LPKG_CHERRYUSB_IPHETH_PATH="packages/third-party/cherryusb-ipheth"
```

前者相对 `application/rt-thread/rtcarplay/` 解析，后者相对 SDK 根目录解析。需要对比独立 checkout 时可在 menuconfig 中改为绝对路径。修改 submodule源码后，在内置路径上增量执行 `scons -j8` 即可重新编译；若要让工程记录新版本，需在 submodule 仓库提交后更新 SDK 的 gitlink。

## 3. 环境与首次构建

开发环境为 Linux、Python 3、SCons、GNU Make、C 编译器及 `patch`。主机测试另需相应的 mbedTLS 开发依赖，具体以 `packages/third-party/rt-carplay/README.md` 为准。SDK 的 `tools/toolchain/` 已有 Xuantie RISC-V 工具链压缩包；本方案选择 V2.6.1，构建脚本自动准备根目录的 `toolchain/`。交叉编译使用 SDK 工具链，不使用系统 `gcc` 生成板端固件。

### 3.1 全新 clone 的构建顺序

先按第 1 节初始化两个 submodule，并获取 Git LFS 工具链和 Realtek 库。CherryUSB 裸机适配已在 `packages/third-party/cherryusb-luban` 中补齐，应先从本仓库编译 Bootloader，再构建应用，无需手工生成镜像地址配置。

```sh
scons --apply-def=d21x_demo128-nand-carplay_baremetal_bootloader_defconfig
scons -j8
scons --apply-def=d21x_demo128-nand_rt-thread_rtcarplay_defconfig
scons -j8
```

Bootloader 构建会生成 `target/d21x/demo128-nand-carplay/pack/bootloader.bin` 和 `.image_cfg.json.tmp`；后者替换 `CONFIG_AIC_BOOTLOADER_*` 地址占位符。统一 CI 构建入口和 GUI/Headless 产物说明见 [固件 CI](zh-CN/ci.md)。此流程会核对打包 Bootloader 与本次编译二进制完全一致。

### 3.2 日常与清理构建

已有 `.config` 和工作区时直接执行 `scons -j8` 做增量编译。需要重新应用方案时执行 `scons --apply-def=d21x_demo128-nand_rt-thread_rtcarplay_defconfig`；应用 defconfig 会重置当前 `.config`，因此不要在每次增量编译前重复 apply。

`scons -c` 可清理中间产物后重新构建。Windows 上偶尔会以非零状态退出并提示 `.sconsign.dblite` 写入失败，这是文件被占用导致的，可删除该文件后重试，或直接继续 `scons -j8`。`.config`、`cconfig.h`、`rtconfig.h`、`generated/` 和 `output/` 都是本地生成结果；需要长期保留的配置应更新对应 defconfig。

### 3.3 方案信息

`--apply-def` 之后用 `scons --info` 核对当前方案：

```text
CONFIG_PRJ_CHIP="d21x"
CONFIG_PRJ_BOARD="demo128-nand-carplay"
CONFIG_PRJ_KERNEL="rt-thread"
CONFIG_PRJ_APP="rtcarplay"
```

defconfig 文件名保留 `demo128-nand`，实际板目录是 `target/d21x/demo128-nand-carplay/`，两者不要混淆。配置入口在 `target/configs/d21x_demo128-nand_rt-thread_rtcarplay_defconfig`。

## 4. 产物、版本与烧录

产物目录：

```text
output/d21x_demo128-nand_rt-thread_rtcarplay/images/
```

| 文件 | 用途 |
| --- | --- |
| `d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img` | NAND 整包烧录镜像 |
| `d21x.elf` | 符号与调试 |
| `d21x.map` | 链接位置、符号和内存占用核对 |
| `d21x.bin` | 应用二进制 |
| `data.fatfs` | 随镜像生成的数据分区 |

使用板卡现有烧录工具烧录 `.img`；串口为 115200、8N1。当前整包刷新会覆盖`/data`，所以刷机后首次 Trust 和重新生成配对记录是预期行为；同一次刷机后的正常拔插应复用已保存记录。不要通过反复刷机验证“同一信任记录重连”。

SDK 镜像输出名不包含 v25/v26 等测试标签，后续构建会覆盖它。交付时另存镜像、SDK 与两个 submodule 的提交号、脏文件状态、defconfig、构建日志及 SHA-256。例如在 SDK 根目录记录：

```sh
git rev-parse HEAD
git status --short
git submodule status
git -C packages/third-party/rt-carplay status --short
git -C packages/third-party/cherryusb-ipheth status --short
sha256sum output/d21x_demo128-nand_rt-thread_rtcarplay/images/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img
```

## 5. 验证与诊断

固件的日常启动由 LVGL 页面完成。状态命令：

```text
carplay_status
carplay_video status
ipheth_status
```

定位视频供给停顿时，保持同一 USB 连接和持续动态画面，进行Carrier on 30 秒 → off 30 秒 → on 30 秒的单变量实验：

```text
ipheth_probe carrier off
ipheth_probe carrier on
```

每次切换后跳过第一组混合窗口。off 保留初始查询和最后已知 carrier 状态，拔插后恢复默认 on。对比 5 秒聚合的 RX/show、RX 错误分类、Carrier 等锁和completion→rearm 延迟，不能只看静止界面的 FPS。测试期间保持解码线程、公告 FPS、TCP 配置和 HCD 行为一致。

USB/HCD 专项测试在 SDK 根目录运行：

```sh
sh packages/third-party/cherryusb-luban/tests/run.sh
sh packages/third-party/cherryusb-luban/tests/run_control_probe.sh
python3 packages/third-party/zephyr-bluetooth/tests/run_br_keys.py
python3 packages/third-party/zephyr-bluetooth/tests/run_enable_settings.py
```

驱动测试在 `packages/third-party/cherryusb-ipheth` 运行：

```sh
cd packages/third-party/cherryusb-ipheth
make sanitize
make sanitize-class CHERRYUSB_ROOT=../../cherryusb
```

协议与平台回归在 `packages/third-party/rt-carplay` 运行：

```sh
cd packages/third-party/rt-carplay
make -j8 check
make check-lubanlite-usb CHERRYUSB_ROOT=../../cherryusb
make check-lubanlite-demo SDK_ROOT=../../..
make check-lubanlite-bonds SDK_ROOT=../../..
make check-rtthread-multicast
```

这些主机测试不会编入板端固件。保留取消、拔插、DMA 和边界回归；UART 默认只保留阶段、警告、错误及低频聚合统计。RT-Thread 的逐包调试日志需显式定义`RTCP_LOG_DEBUG`，默认构建关闭。

更多 USB 补丁、缓存风险和验证边界见[`cherryusb-luban/README.md`](../../../../packages/third-party/cherryusb-luban/README.md)，视频因果实验见外部 `rt-carplay/docs/wired-video-stall-probe.md`。

## 6. 无线与蓝牙配对（v27）

默认启动页选择 USB；点击 Wireless 后，同一个工作线程先关闭旧会话，再启动Realtek AP、DHCP、AirPlay 和蓝牙 iAP2 服务。在 iPhone 蓝牙设置里选择`rt-carplay`，按提示确认 CarPlay；出现六位数字时在两端核对确认。页面显示Wi-Fi SSID/IP、蓝牙／MFi 阶段、最近 HCI/security 结果和 bond 存储状态。收到实际解码帧后才进入演示，不能把 Bluetooth listening 当作画面已就绪。

本方案默认启用 `CONFIG_RTCARPLAY_BT_PERSISTENCE=y`，它同时选择 SDK 的`SETTINGS` 和 `LPKG_BT_SETTINGS`。实际后端是外部 app 的`port/luban-lite/rtcp_bt_settings.c`；SDK 的 Zephyr autoconf 和 D213 linker script 必须与这版应用一起使用，否则会缺少 settings 或静态处理器。蓝牙密钥保存到 `/data/bt_settings.bin`，接收器启动时恢复，再发布蓝牙服务。本方案设 `CONFIG_LPKG_BT_MAX_PAIRED=4`（SDK 原默认只有 1）；界面显示已用／上限，满后手动忘记旧配对，不启用静默淘汰旧手机的策略。整包刷机后手机可能仍留有旧 bond，应先忘记旧设备再重新配对；重连验证使用普通重启，不能再次刷机清空数据。

Pair files 页面保留 USB Trust／AirPlay 文件管理，并增加 Forget Bluetooth pairs。它会通过蓝牙栈删除 BR 配对、保留本机身份；不要在运行时直接删`bt_settings.bin`。板端删除后，手机也应忘记该设备。

本版没有混入 USB/HCD、解码、公告 FPS 或 TCP 参数优化，方便继续做第 5 节的Carrier A/B。新 iPhone 的失败原因仍要按 SSP、iAP2/MFi、Wi-Fi、AirPlay 阶段区分；具体测试矩阵见外部 `rt-carplay/docs/wireless-bringup.md`。

v27 曾出现首次刷机后 `bt_settings_init()` 返回 `-5` 的启动回归：DFS 的`open()` 将文件缺失记录为负 errno（`-ENOENT`），应用蓝牙后端却只匹配正 `ENOENT`。请使用 v28 或更新版 rt-carplay：它对主文件／备份都匹配两种符号，保留真实 I/O 错误。`make check-lubanlite-bonds` 同时运行正／负 errno回归。蓝牙配对前应先看到 `settings storage ready`；这次错误发生在控制器初始化和手机配对之前，不应通过删除其他配对记录或修改 Wi-Fi 参数绕过。SDK 还在这条 settings 早期失败路径清除 `BT_DEV_ENABLE`，让存储恢复后能重试，避免 `-EALREADY` 被误认为控制器已初始化。上述 enable-settings 主机测试隔离编译实际 `bt_enable()`，验证失败时未打开 HCI，以及重试和重复启用。

v29 补齐 RTSP 建立前的 Control 单播发现：从有效的本机 AirPlay mDNS 查询中学习 AP /24 内的候选地址，再查询其 `_carplay-ctrl`；仍须解析到实际 SRV才发起 Control 连接。不会硬编码手机 IP 或端口。LVGL 及 `carplay_iap2 status`新增 Availability、Invite 和链路 ACK 计数，MFi 成功记录保留至服务重启。无线已关联但无画面时，请从启动阶段抓日志，并保存 `carplay_iap2 status`、`carplay_bt status`、`carplay_ctrl status`、`carplay_status`。具体判读见外部`rt-carplay/docs/wireless-bringup.md` 的 v29 段；此版不能视为已完成板端激活验证。

v30 对应“无线已有视频，但 iPhone 一直 Connecting”的连接收尾验证版。外部 rt-carplay 增加 AirPlay type-130 的加密 iAP2 隧道，出站通过 event `iAPSendMessage`；收到 `disableBluetooth` 后，等隧道 MFi 与 Identification完成才释放 bootstrap ACL。`carplay_iap2 status` 新增 tunnel/identified/ authenticated/handoff_complete；首帧不等价于交接完成。代码已补齐此前“声明 iAPChannel、实际拒绝 type-130”的缺口，弹窗消失仍待上板验证。本版无需调整 defconfig，仍在本 SDK 工作树执行 `scons -j8`；构建读取`/home/boa/Documents/rt-carplay` 外部源码。细节及证伪条件见外部`docs/wireless-bringup.md` 的 v30 段。配套 rt-carplay 提交：`95be132`（v30）；可额外运行`make check-lubanlite-handoff` 验证实际交接函数的异步 HCI 生命周期。


## 7. 音频开发分支与构建

音频修改分两层：SDK 本目录保存 defconfig/Kconfig 与打包环境，`packages/third-party/rt-carplay` 子模块保存实际协议、编解码器、混音、sound0/dmic0 适配及测试源码。SDK 构建直接读取该子模块源码，不需要把协议复制进 SDK。子模块版本由 SDK gitlink 固定；升级协议栈时在子模块内提交并更新 SDK 的 gitlink。

在 SDK 根目录执行：

```sh
scons --apply-def=d21x_demo128-nand_rt-thread_rtcarplay_defconfig
scons --info
scons -j8
```

默认方案保留 USB/Wireless LVGL 选择页并启用：

```text
CONFIG_RTCARPLAY_AUDIO=y
CONFIG_LPKG_RT_CARPLAY_AUDIO_CODECS=y
# CONFIG_RTCARPLAY_MICROPHONE is not set
```

下行支持 LPCM、FAAD2 固定点 AAC-LC 和 Opus；`/info` 根据实际编译的能力发布 102/media AAC-LC，100/101 的可用 PCM/Opus。接收包通过各角色独立的jitter queue 和 decoder，在专门音频线程重采样混音为 48 kHz 双声道，输出 sound0。手机音量/duck 为软件增益，给本地关键告警预留不受远端静音影响的通道。不能把 `SETUP 200` 当作喇叭已经发声。

麦克风构建在 `scons --menuconfig` 中打开 `RTCARPLAY_MICROPHONE`，要求`AIC_AUDIO_DMIC=y`；这会发布上行能力并使用独立 Input Encryption Key，经 AirPlay SETUP 的 dataPort 发送 mono LPCM/Opus，不复用 Bluetooth HFP。`RTCARPLAY_MIC_AEC` 为单独的实验开关，`RTCARPLAY_MIC_AEC_DELAY_MS` 范围0–200、默认 30 ms；实际回声路径需要校准。dmic0 注册成功不能证明物理采集通道正确。标准镜像不默认打开麦克风/AEC，避免发布未验证的实体输入。本轮分别完成了默认编解码构建与麦克风/AEC 全开构建，均不等于实机通过。

音频配置说明以 `packages/third-party/rt-carplay/examples/sdk_app/Kconfig` 为准，本 SDK 中`application/rt-thread/rtcarplay/Kconfig` 保持同步。第三方源代码已经随 app纳入构建，不需要下载二进制 codec：FAAD2 GPL-2.0-or-later，Opus/SpeexDSP BSD-3-Clause；来源、版本和保留声明见 app `third_party/audio/README.md`。Code from FAAD2 is copyright (c) Nero AG, www.nero.com. AAC-ELD/Main Buffered/Enhanced Siri 不宣告为已支持。

Host 回归在 app 根目录运行：

```sh
make -j8 check
make BUILD=build-pcm AUDIO_CODECS=0 -j8 check
cmake -S . -B build-cmake -DRTCP_SANITIZE=ON
cmake --build build-cmake -j8
ctest --test-dir build-cmake --output-on-failure
make check-lubanlite-demo check-lubanlite-handoff SDK_ROOT=../luban-lite-carplay
cmake -S . -B build-bench -DRTCP_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench -j8 --target audio_bench
./build-bench/audio_bench
```

54 项测试覆盖协议、真实 AAC/Opus、加密上行、并发同类型音频流、选择性TEARDOWN、重连、混音/安全告警和实际音频 worker 的 Host 模型。ASan 对音频第三方库启用；UBSan 不用于其刻意的定点移位/溢出算法。Host CPU/heap 基准在 app `docs/audio-host-benchmark.txt`，不得外推为 D213 CPU 使用率。

板端先从 LVGL 进入 Wireless，再播放音乐，检查 `carplay_status` 的音频包数及 write errors，期望看到 `audio_setup=ready type=102` 或实际协商的100/101。麦克风版本使用 `carplay_mic`、`carplay_mic mute|unmute` 和`carplay_siri`。同时记录 `list_device`、`list_thread`、`free`。必须实测：音乐路由和音量、DMIC 接线、Siri/电话双向、导航混音、声学回声、持续 CPU/栈/heap、30 分钟漂移，以及音频负载下的视频稳定性。完整功能矩阵与真实限制见 app `docs/audio-subsystem.md`。

## 8. 音乐仍从 iPhone 输出的后续修复

请使用包含 discovery/clock 修复的 app `main`。此前即使音频 worker 已打开sound0，mDNS TXT 仍落到无音频默认掩码 `0x5203a4e2,0x61`，而 `/info`已宣告有音频，导致发现与会话能力不一致。新版本两处使用同一有效掩码，默认启动应打印：

```text
[carplay] capabilities features=0x5653aee2,0x61 audio_output=1 compressed=1
```

同时修复 RT-Thread 初始 tick `0xffff0000` 在约65秒回绕导致的时间倒退。AirPlay timing、音频 jitter 与 AEC reference 共用扩展后的单调时钟。日志中 `max_total_us=4294967295` 不能直接当作真实阻塞时长。

更新 app 后在本 SDK 执行 `scons -j8`，刷入镜像并重新连接 Wireless，然后播放音乐、执行 `carplay_status`。新增 `audio transport` 行报告`streams/datagrams/auth_failures`：流数为0时排查 SETUP/路由，已有流但无包时排查 UDP，收到包且 sink packet 增长而无声时再定位解码/声卡输出。`carplay_iap2 status` 的 `handoff_complete=1` 只证明交接，不能证明音频路由。

本次 Host 55项测试、PCM-only、ASan/UBSan CTest、LVGL/handoff 回归以及SDK 默认构建均通过。另运行 app `make check-audio-discovery`，直接启动真实Linux 应用、从网络读 mDNS TXT 和 RTSP `/info`，验证音频开/关两种模式逐位一致。手机路由修复效果尚待实机复测，详细原因和判读见 app `docs/audio-routing.md`。

后续实机反馈仍为 `streams=0 datagrams=0`，完整连接日志没有音频SETUP；因此discovery/clock修复并未解决该手机的音频路由。继续修复应用 `/info`始终发布空 `bluetoothIDs` 的遗漏：现在每次接收AirPlay连接时，从已启用的控制器读取与iAP2一致的真实地址，再生成并复制该session的能力文档。当前session的副本保持不变，重连时重新读取；不硬编码蓝牙MAC、不强制抢占MainAudio。尚不能把该遗漏认定为唯一根因。

新镜像在连接时输出 `session capabilities bluetoothID=...`，第一次实际响应`/info` 时输出 `info=served ... audio_formats=9` 和逐项 `info_audio` 位图。默认type102/media应为 `output=0x400000`（44.1 kHz AAC-LC）。刷入后重新连接，保留这些日志、播放音乐前后的 `carplay_status` 及后续SETUP日志；若仍无流，就继续定位协商/路由，不将其误判为sound0或解码器故障。

身份刷新版本已通过55项Host、PCM-only、ASan/UBSan CTest、discovery、LVGL/handoff测试和SDK默认构建（无编译警告），尚未经过手机实测。不同构建目录的socket测试须顺序运行，避免共用默认7000端口造成测试冲突。当前镜像仍在上文images目录，SHA-256为`1af7ab53f843e58e8fd6d35a851760e36338fd0995020334561906f0e95dbdd7`。

## 9. 无音频SETUP：printf与媒体能力宣告修复

新的实机反馈确认视频正常，但仍没有100/101/102音频SETUP。上节身份刷新尚未使该手机建立音频流，不能当作音频问题已解决。用户日志中的`features=0x%lx audio_formats=1448324834` 是本SDK关闭`RT_KPRINTF_USING_LONGLONG` 后不支持 `%llx` 的结果，后一个 `%u` 错读了Features低32位，并不意味着plist内真的有14亿个格式。本轮协议日志改成两个32位参数，未改RT-Thread全局配置。

对照公开catplay接收端修复协商差异：补齐实际支持的AudioPCM位18；AAC-LC位20仅在codec编译并宣告时启用；默认同时提供44.1/48 kHz媒体格式；102的latency角色由default改为media，与格式条目对应。原有解码器、混音和sound0后端不改，蓝牙交接及资源模式不改。

新镜像的默认日志应为：

```text
[carplay] capabilities_check txt_features=0x5657aee2,0x61 info_match=1
[iap2] identity bluetoothID=<真实地址>
I/airplay_rtsp_session: info_bluetooth count=1
I/airplay_rtsp_session: info_bluetooth index=0 id=<与iAP2一致>
I/airplay_rtsp_session: info=served bytes=... features=0x000000615657aee2 audio_formats=9
I/airplay_rtsp_session: info_audio type=102 role=media output=0x0000000000c00000 input=0x0000000000000000
```

`info_match=1` 比较编码后的TXT与plist，而非只比较配置变量。`info_bluetooth` 从实际返回文档读取。明确限制采样率仍可用`CARPLAY_APP_AUDIO_SAMPLE_RATE=44100/48000`；默认0的新含义是提供两种速率。PCM-only掩码为 `0x615647aee2`、格式数8；关闭音频时不宣告音频。

Host额外运行：

```sh
make check-lubanlite-capabilities SDK_ROOT=../luban-lite-carplay
make check-audio-discovery
```

前者使用本SDK真实 `rt_vsnprintf`（关闭long-long）执行app/协议实际函数，覆盖数字/后续参数、蓝牙ID及TXT/plist校验。55项Host/ASan/UBSan CTest、PCM-only、discovery、LVGL/handoff和SDK默认构建全部通过，无SDK编译警告。这些结果仍不能证明iPhone已经发送音频SETUP；刷入后记录完整连接与播放日志，以 `setup=stream_candidate` 的100/101/102及后续包数增长验收。

本节构建替代前两节音频排查镜像，路径不变。当前镜像SHA-256：`9af6c4ad1121c1372276b70ab655f8c4396a9aa996d2c12496922d6487c016b0`。参考版本、具体差异及实机判读见app `docs/audio-negotiation.md`。

## 10. 已配对 iPhone 的 READY 重连：密钥更新与恢复保护

本轮应用基线为 `rt-carplay/main` 的 `0d65737`，该版本现由 SDK gitlink 固定。SDK 除本文外修改 `classic/ssp.c`、`classic/keys_br.c` 和 `classic/br.c`，不改变音频能力公告、解码器、Mixer 或 sound0。

确认并修复一条会破坏旧 bond 的路径：HCI Link Key Notification 的Changed Combination Key（0x06）此前被当成 unsupported，保存全零密钥并清掉认证属性；现在继承原 key 的认证／SC 属性，持久化更新后的密钥。缺少原 bond 或不满足 SC-only 时拒绝更新，其他未知类型也不覆盖旧密钥。恢复长度不匹配的非空 BR 记录不再被删除或截断后当作有效记录接受。

应用在 settings 恢复失败时阻止发布服务／进入 READY，回放表满时保留文件，并补充恢复计数和 ACL/security 状态。SDK 记录 incoming connection、是否命中saved key 和 HCI authentication 结果，不打印密钥内容。

本轮验证：

* `make -j8 check`：55 个 Host 测试及边界检查通过。
* `make check-lubanlite-bonds SDK_ROOT=../luban-lite-carplay`：实际后端的
  正／负 errno、文件恢复、错误传播和容量保护通过；实际 SDK 的地址编解码、BR handlers、HCI key request / notification 以 ASan/UBSan 运行通过。覆盖三次冷启动、密钥更新后重启、普通／SC-only、未知类型和无原 bond 拒绝。使用修改前的 `ssp.c` 跑同一回归，在更新后的 key/flags 检查失败，符合预期。
* `check-lubanlite-demo`、`check-lubanlite-handoff`、`check-lubanlite-capabilities`
  通过，原无线交接及能力公告检查保留。
* SDK `tests/run_enable_settings.py` 的初始化失败／重试测试通过。
* `scons -j8`：D213 SDK 构建通过，本轮编译无 warning/error。
* 尚无此版本固件在真实 D213／iPhone 上重连成功的验证。

镜像路径（沿用默认输出路径）：

```text
output/d21x_demo128-nand_rt-thread_rtcarplay/images/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img
```

本轮镜像替代第 9 节镜像，SHA-256：`c04bdba8379d84fb1e942e9b1b145cd5da72385e35b682aff35d5af35545f7c6`。

刷入时保留 data 分区和手机配对；先不要 Forget Device。启动后应看到`Bluetooth settings loaded: BR bonds=... storage_error=0`。在 READY 点击手机已有设备连接，保存完整日志，然后执行：

```text
carplay_bt status
carplay_iap2 status
carplay_status
```

有 `replying with saved link key` 后应看到认证 `status=0x00`、security `error=0`和 RFCOMM connected。如果一直没有 incoming connection，说明尚未进入密钥应答阶段；如果应答后认证报 0x05/0x06，重点检查现存双方 bond 是否失配。已被旧固件保存成错误密钥的 bond 无法由新固件反推出手机端正确密钥，不能保证保留它就会自动恢复。只在收集旧配对重连失败日志后，再做双方重新配对对照。详细判读与实机矩阵见应用 `docs/bluetooth-bond-reconnect.md`。

## 11. READY 自动重连与手动 Connect 无事件

应用基线为 `rt-carplay/main` 的 `4e84598`，SDK 的 HCI 同步完成隔离修复为`ddeeaaab2`。本节替代第 10 节的镜像，前一节密钥更新／存储保护仍保留。

此前 READY 只代表设置了 connectable/discoverable 并在无限 accept 中等待手机，没有已配对设备的主动连接策略，也没有 HCI Read Scan Enable 的控制器读回。现在注册服务后核对 scan=0x03；发现 host 缓存与控制器不一致时 off/on 修复，读回失败则不发布 READY。空闲 owner 每 500 ms 运行一次，首次延迟 2 秒主动寻呼已保存的 BR bond，未建立 RFCOMM 的尝试间隔 30 秒，多个 bond 轮询。ACL 建立后复用旧密钥请求加密，并等待手机接入既有 iAP2 RFCOMM 服务。20 秒超时释放本 owner 发起的连接；入站连接／配对、Wi-Fi event／tunnel 接管期间不继续寻呼，也不会由重连定时器关闭已经接入的同一手机 RFCOMM。

这是实际主动 ACL 连接实现；iOS 是否据此自动打开 RFCOMM 并完成 CarPlay仍需实机验证，没有猜测手机的 RFCOMM channel 或宣称 Host 测试替代了手机。手动 Connect 无日志也尚不能被认定为密钥问题，应先观察 HCI／ACL 阶段。

SDK 的 `bt_hci_cmd_send_sync()` 原先共用一个静态信号量，并发命令可能唤醒错误的调用者。现在按命令缓冲池槽位独立等待并复用对象；当前 ELF 的等待数组为 624 字节 BSS，运行时 RTOS 对象另计，没有新增线程。Host 双线程测试覆盖顺序／倒序完成、独立错误／响应、引用平衡和三次槽位复用；修改前代码不能通过独立等待对象断言。应用层新增 incoming Classic page 日志，不依赖 SDK info过滤设置，状态命令显示扫描读回、radio error、尝试数和 active ACL 数。

验证结果：55 项 Host 测试及边界检查、READY 状态机 ASan/UBSan、原 bond持久化／密钥更新回归、LVGL、handoff、能力公告检查均通过。SDK HCI 并发与初始化失败／重试测试通过。D213 `scons -j8` 构建通过，本轮无编译 warning/error。尚无本轮 D213／iPhone 自动连接的实机验证。

```sh
# rt-carplay 子模块
cd packages/third-party/rt-carplay
make -j8 check
make check-lubanlite-reconnect check-lubanlite-bonds check-lubanlite-demo \
  check-lubanlite-handoff check-lubanlite-capabilities \
  SDK_ROOT=../../..
# SDK 仓库
python3 packages/third-party/zephyr-bluetooth/tests/run_hci_sync.py
python3 packages/third-party/zephyr-bluetooth/tests/run_enable_settings.py
scons -j8
```

镜像路径不变：

```text
output/d21x_demo128-nand_rt-thread_rtcarplay/images/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img
```

SHA-256：`32c13ee017157aebcb515154bded94689191388ee28c16e52cdd5482f18768dc`。

保留 data 和手机配对，启动选择 Wireless，不先 Forget Device。首先应看到：

```text
READY radio address=... scan=0x03 (page+inquiry)
bond reconnect enabled: first page in 2s, retry 30s; Wi-Fi handoff pauses paging
reconnect paging bonded peer=... scan=0x03
```

手动点击手机已配对设备 Connect，保存其前后至少 30 秒串口日志，并执行：

```text
carplay_bt status
carplay_iap2 status
carplay_status
```

* 没有本节的启动标记：先核对镜像或启动失败位置，不能拿旧版日志判断新状态机。
* bond 数为 0：没有可寻呼的已恢复 BR bond，文件存在不等于有 bond。
* scan unknown／radio error：先检查 H5／控制器命令。
* 有主动 paging 后 HCI 0x04：Page Timeout，尚未验证密钥；查手机可连接性、
  地址和射频／Wi-Fi 共存。读回 0x03 只证明设置值，不证明空中收发。
* saved-key reply 后 HCI 0x05/0x06：已经进入认证，继续排查旧密钥失配。
* ACL/security 成功而 RFCOMM 未起：继续定位 iOS profile 建立，不能当作
  CarPlay 已自动连接成功。新的日志会明确显示 awaiting phone RFCOMM。

细节、Host 测试边界和实机矩阵见应用 `docs/bluetooth-bond-reconnect.md`。

## 12. 已配对手机自动连接的实机反馈与 H5 修复镜像

用户提供的上一版镜像日志已确认：恢复 BR bond 后主动 paging、saved-key reply、认证 `status=0x00` 和 security level 2 均成功。第一次 ACL 被手机以 `0x14`（Remote Device Terminated Connection due to Low Resources）关闭；30 秒后的重试建立 RFCOMM，完成 MFi、Wi-Fi、认证后的 iAP2 tunnel 和视频首帧 `stage=5`。这是一次自动连接成功的证据，尚不能据此保证每次重连可靠。BR 地址和 Wi-Fi私有地址不同是正常现象，不应据此清除配对。

该日志在 AirPlay pairing 附近暴露了连续 H5 序号不匹配和`Unexpected HOST_NUM_COMPLETED_PACKETS, status 0x12`。本节镜像修复：

* ACK、TX、重传和链路初始化按完整 UART 帧互斥，避免逐字节交叉；RX 回调在
  此端口的 `bt_uart_wq` 线程执行，允许使用互斥锁，交付 Host 前释放锁。
* 遵守协商发送窗口；合并的 TX 通知会填充可用窗口；ACK 释放窗口后唤醒 TX。
* 未确认帧在原队列按原序号重传，避免序号回退／队列搬移与 ACK 并发；窗口外
  ACK 不再导致无符号计数下溢和无限循环；新流量不推迟最老帧的重传定时器。
* 重复 RX 帧重新 ACK，不再次交付 HCI；分段 SLIP escape 不忙等；校验头部
  checksum、最小长度和未协商的 CRC 标志，避免空事件交付空指针。
* ACL 接收缓冲从分配开始标记为尚未归属连接。传输层丢弃的重复／残缺帧不
  上报新的 Host Number Of Completed Packets；回调也排除已被另一 handle 复用的连接索引。此前清零的 index=0/handle=0 会被误认为连接 0 的完成计数。

Host 测试直接编译实际 H5 状态／收发／重传函数、实际 `bt_buf_get_rx` 和`bt_hci_host_num_completed_packets`，并模拟 UART、队列和连接。覆盖窗口、序号回绕、无效 ACK、重传与 ACK 竞争、200 帧并发发送、转义拆分、重复／残缺ACL 丢弃、连接索引复用、完成计数的字节编码及缓冲释放平衡。ASan/UBSan 通过。将分配器和完成回调替换回修改前的版本，同一测试会因生成错误 handle/count而失败。这是 Host 复现，尚非 D213 端 `0x12` 已消失的验证。

```sh
python3 packages/third-party/zephyr-bluetooth/tests/run_h5.py
python3 packages/third-party/zephyr-bluetooth/tests/run_hci_sync.py
python3 packages/third-party/zephyr-bluetooth/tests/run_enable_settings.py
scons -j8
```

以上测试、应用侧 reconnect/bonds/demo/handoff/capabilities 回归及 D213 SDK构建均通过。本轮最终 SDK 构建日志无 warning/error。新镜像：

```text
output/d21x_demo128-nand_rt-thread_rtcarplay/images/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img
SHA256 85653fe910f2ed82c2be4af16ddf78df514595e45384c8562ade3869150c959a
```

保留 data 和手机配对，选择 Wireless。新版本启动应出现：

```text
H5 transport: serialized frames, bounded window, in-place retransmission
READY radio address=... scan=0x03 (page+inquiry)
reconnect paging bonded peer=...
BR authentication complete: status=0x00 ...
RFCOMM DLC connected
wireless handoff complete: tunnel authenticated/identified; Bluetooth released
[carplay-demo] stage=5 error=0
```

保存 READY 后至少 90 秒和接管前后的完整日志；重复冷启动、手机蓝牙关闭／开启、退出 CarPlay 再连接。记录重试次数、认证结果和 `H5 sequence retry` 累计计数。该计数保留诊断并按 1、2、4、8…次打印，避免重传风暴刷屏；偶发重传本身不等于数据丢失。重点检查是否仍有 `HOST_NUM_COMPLETED_PACKETS status 0x12`，不能仅凭旧 `Seq expected` 文本不再出现断言传输已修复。运行：

```text
carplay_bt status
carplay_iap2 status
carplay_status
```

已知限制：本轮无板卡复验；没有证明第一次远端 `0x14` 由 H5 引起。接管主动断开时的 RFCOMM `-110` 不单独判为连接失败。本日志仍只有视频 type=110，没有音频 SETUP；蓝牙传输修复不能算作音频路由问题解决。`/info` 蓝牙 ID 和 TXT/plist Features 已在实机日志匹配；下一步音频诊断仍以 iPhone 发起 type=100/101/102 为准。静态页面时视频零更新不单独判定画面冻结。

## 13. 音乐不建立 SETUP：参考协议复核与定位固件

应用 main `6d0b2a5`。用户已刷入第 12 节 H5 固件并连接视频，实际 `/info`身份、Features、非空 PCM/AAC 格式均已确认，但仍没有 100/101/102 音频SETUP。本轮深入对照 DiPlay、catplay 和 LIVI，未找到可以唯一解释真实 iOS路由选择的确定根因。详细源码位置、差异表和实验边界见应用`docs/audio-routing-reference-review.md`。

确认：entity=1 表示 iPhone/Controller，不表示手机扬声器；当前 owner 和permanent owner 不等价。无麦克风时线上输入字段已经省略，旧 input=0 日志不能证明发出了一个零值输入字段。DiPlay 允许不开 mainBuffered，本项目不靠假宣告 type103 或强行 Take MainAudio 诱使手机选择输出。

本轮补齐模式通知的 current/permanent owner 解析和 delta 快照；首次 /info从真实文档打印初始资源；控制连接前 32 个请求记录方法／路径／状态，并为后续错误响应保留 8 次输出；未知命令记录净化后的类型名。没有打印请求正文、密钥或认证材料。本固件的目标是定位协议拒绝和资源归属，**尚未证明解决iPhone 音乐输出问题**。

```sh
# 应用仓库
make -j8 check
make check-lubanlite-capabilities check-lubanlite-demo check-lubanlite-handoff \
  SDK_ROOT=../luban-lite-carplay
# SDK 仓库
scons -j8
```

55 个 Host C 测试、boundary、SDK 小 printf 和应用集成回归通过。新模式用例覆盖 current/permanent owner 不同、仅单个字段的 delta 和非法实体不执行回调。SDK 构建通过，最终构建无 warning/error；尚无本轮板端验证。镜像路径不变：

```text
output/d21x_demo128-nand_rt-thread_rtcarplay/images/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img
SHA256 84f8c277be54922a7c7feb0971c0d7b6fc8b3a325942c3c50629974a823e6e12
```

保留配对，冷启动连接 Wireless。在 CarPlay 音乐界面选曲并播放，保存连接及播放时日志。检查 iPhone 的音频输出选择器中 CarPlay 是否存在、是否被选中。播放前后分别执行 `carplay_status` 和 `carplay_iap2 status`。新增关键行：

```text
info_resource id=2 transfer=1 priority=100 take_constraint=100
negotiation id=1 request=... method=POST path=/command body=... status=200 encrypted=1
modes_changed count=... main=1 permanent=1 screen=1 screen_permanent=2 ...
carplay audio resource: permanent=1 screen=1 screen_permanent=2 (0=none 1=iPhone 2=accessory)
```

实际值以手机通知为准；unchanged/ 表示本次 delta 未携带字段，unknown/ 表示会话快照尚未观察到该字段。若有 400/405/503，以实际路径／命令定位；若MainAudio 当前及永久 owner 都为 1、请求成功却没有路由条目，需继续对照同一手机的路由注册行为，不能宣称 codec 修复。只有实机出现音频 SETUP 才能算协商阻塞解除；有包且鉴权成功后才转查音频后端。

## 14. MainAudio owner 正常后的 timing 修正及交换状态

应用 main `1c9283b`。第 13 节固件的最新实机日志已经确认 MainAudio 和MainScreen 的 current/permanent owner 全为 iPhone=1，HID_sent=2395、HID_failed=0，iAP2 隧道认证和视频正常，音频仍然没有 SETUP。继续查附件资源抢占或触摸未送达不符合这份证据。iPhone 播放输出选择器是否出现并选中CarPlay，以及 iOS 版本，尚未获知。

进一步对照 DiPlay event 处理、真实麦克风配置和 timing，以及 catplay timing实现，发现一个可复现的 poll 顺序错误：当 timing 应答可读、下一探测周期又已到期时，先发新探测会覆盖旧 pending timestamp，随后误丢弃旧有效应答。改为先处理可读应答再发探测。未改变时钟基准、能力声明、codec 或声音设备。**Host 已证明 timing 缺陷，尚未证明它造成这台 iPhone 不发音频 SETUP。**

`carplay_status` 新增当前 timing endpoint 的计数：

```text
carplay timing: active=1 request_tx=... response_rx=... request_rx=... response_tx=... rejected=... send_errors=... timeouts=...
```

`response_rx` 只计来源、包格式、originate timestamp、RTT 校验通过的应答；不是任意 UDP 收包数，也不代表已经实现完整的媒体时钟同步。`timeouts` 是下一探测到期时上次仍未收到有效应答的次数。端点关闭计数清零。应用在 RTSP owner 线程采集并经已有 mutex 提供状态，shell 不访问端点指针。

验证：55 项 Host 测试和 boundary 通过；固定时钟/loopback UDP 新用例覆盖到期应答、重复应答和超时。恢复旧顺序时新用例确定失败，新顺序通过。SDK formatter、demo、handoff 检查及 D213 `scons -j8` 通过，无 warning/error。实际 Linux 应用 TXT 与 `/info` 经 Python plistlib 解析一致（本地 sim 为PCM-only，不能代替压缩启用的 D213 线上抓包）。本轮没有连接板卡。

新镜像：

```text
output/d21x_demo128-nand_rt-thread_rtcarplay/images/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img
SHA256 19d06db660f8b1a06a8d2e49b7dfb814e29ff3c7032396ba403a3a2b183acb9d
```

保留配对，连接并在 CarPlay 音乐界面播放，间隔 5 秒各运行一次`carplay_status`。记录 iOS 版本和手机输出选择器中的 CarPlay 条目及选中状态。如果 `response_rx` 持续增长而仍无音频 SETUP，不再把缺少 timing 往返当作解释；如选择 CarPlay 后出现音频 SETUP，分别记录自动/手动路由行为，不能把它自动算作 timing 补丁的效果。request_tx 增长而 response_rx=0 时，依据其余计数进一步检查 timing UDP。验收仍为实机出现 type100/101/102 SETUP，本镜像不标记为已解决音乐接入。

## 15. iOS 27.3 timing 复验与精简 Features 对照镜像

应用 main `431d1cf`。用户提供 iOS 27.3 实测：timing request_tx/response_rx从 34/34 增至 41/41，rejected/send_errors/timeouts 均为 0，音频流和收包仍为0，输出选择器没有接收端。MainAudio current/permanent 已从 2 变为 1。因此上一节修正没有解除音频 SETUP 阻塞，也没有证据继续归因于 timing 往返不通或 owner 卡在附件。选择器缺项本身不直接证明 iOS 内部路由注册失败。

本轮对照 catplay/LIVI 显式 Features 集合，收窄 RT-Thread 应用的默认掩码。压缩启用时从 `0x5657aee2,0x61` 变为 `0x44540280,0x61`，只取原先已提供能力与明确集合的交集，不新增 Rotate。去掉照片/幻灯片/照片缓存、metadata、iTunes encryption、未解释位和冗余音频发现声明；保留当前提供的 Screen、Audio、PCM、AAC、Unencrypted/MFi、UnifiedBonjour、Car/CarPlayControl、HKPairingAndEncrypt。详细位号及参考位置见应用 `docs/audio-routing-reference-review.md`。

这是单变量兼容性实验，不是已确定的 iOS 27.3 根因修复。没有改变 PCM/AAC/ Opus 格式表（仍 9 条）、麦克风、蓝牙身份、modes、SETUP 响应、AAC 解码、Mixer 或 sound0。两份文档由真实 plist writer 生成后，经 Python plistlib独立解析，验证除 Features 外完全相同，覆盖无音频/PCM-only/压缩三种 offer；各组 TXT/plist 一致。声明不能把无音频变成有音频，也不能为 PCM-only 加 AAC。公共 core 默认值不变；RT-Thread 可定义 `CARPLAY_APP_LEGACY_FEATURES=1`构建原掩码。新日志 `capabilities_profile=explicit` 标记当前配置。

55 项 Host 测试、boundary、SDK formatter/profile、demo/handoff 检查通过。D213 `scons -j8` 构建通过，无 warning/error。尚未连接板卡验证本轮修改。

默认 SDK 镜像仍位于：

```text
output/d21x_demo128-nand_rt-thread_rtcarplay/images/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img
SHA256 7710a774e91076ab81d1b776eadc3f256c78b4fc7528647805bf205109c2a8d7
```

另已保留两份固定文件，后续构建不覆盖：

```text
/home/boa/Documents/rt-carplay-artifacts/ios27-route/carplay-explicit.img
SHA256 7710a774e91076ab81d1b776eadc3f256c78b4fc7528647805bf205109c2a8d7

/home/boa/Documents/rt-carplay-artifacts/ios27-route/carplay-legacy-1c9283b.img
SHA256 19d06db660f8b1a06a8d2e49b7dfb814e29ff3c7032396ba403a3a2b183acb9d
```

先保留配对刷 explicit，在 CarPlay 音乐界面播放。确认实际 /info 日志为`features=0x0000006144540280 audio_formats=9`，再看音频 SETUP 和选择器。如出现 100/101/102 SETUP，用 legacy 复测相同步骤确认差异可重复；不能仅凭一轮成功便认定某一位是根因。如仍不出现，该实验未解决现象，需继续检查同手机的参考接收端实际会话差异，不继续重复添加 timing/owner 日志。

## 16. 无线 explicit 已确认：采集手机初始 SETUP 的功能提议

应用 main `35dcf08`。用户已补充无线 WPA2 后的 explicit 配置、正确的蓝牙身份、4278 字节会话文档和相同 Features，音频仍未建立。第 15 节 Features实验没有解除阻塞；此前另一段 wired 启动日志不能解释这个无线结果。timing 往返和 owner 已验证正常，本轮不继续调整它们或猜测 codec。

初始 SETUP 的现有日志只有 body 长度和本机返回的 enabledFeatures，无法知道 iOS 27.3 实际提出哪些功能。本轮仅增加该提议和版本/timing 字段的有界日志，未改响应、能力表、音频后端或启用 mainBuffered：

```text
setup_offer feature_count=... limit=16
setup_offer index=0 name=...
setup_peer osVersion=...
setup_peer sourceVersion=...
setup_peer timingProtocol=...
setup=initial ... enabled_features=iAPChannel,viewAreas
```

每个提议最多 63 字节、最多 16 项，净化控制字符；超长/非字符串显示invalid_or_overlong，features 缺失/类型错误显示 missing_or_invalid。版本及timingProtocol 只在手机携带字符串时显示。不打印完整请求、密钥或设备标识。日志观察不改变接受/拒绝策略。看到 mainBuffered 只证明手机提出该功能，不能据此认定它必需，更不能用虚假的 type103 应答诱导流建立。

55 项 Host 测试、boundary、SDK formatter/profile、demo/handoff 及 D213 构建通过，无 warning/error。formatter 直接测试真实函数，覆盖 20 项截断、换行净化、超长和非法类型，以及不输出密钥和设备 ID。未连接实机。

SDK 标准镜像路径保持不变，另保留固定文件：

```text
/home/boa/Documents/rt-carplay-artifacts/ios27-route/carplay-setup-offer.img
SHA256 318cc7fb50afc32e80ec3c62fefa55a00f9ccc69572ef30a67997b6d5994b58b
```

这是协商证据采集固件，不标记为音乐修复。保留配对，连接 Wireless，保存setup_offer/setup_peer 及初始 SETUP 响应，再在 CarPlay 音乐界面播放，保留后续音频 SETUP 候选及状态。无需继续重复贴视频性能统计或询问已知的 iOS版本。相关字段用于与同一手机在参考接收端上的实际提议/响应作对照。


## 17. 麦克风输入开启：验证音乐路由假设

用户要求验证缺少麦克风能力是否导致 iOS 27.3 无法将音乐切换到 CarPlay。本次在 D213 defconfig 启用 `CONFIG_RTCARPLAY_MICROPHONE=y` 并显式保留依赖 `CONFIG_AIC_AUDIO_DMIC=y`，`CONFIG_RTCARPLAY_MIC_AEC` 关闭。与保存的完整基线 `.config` 相比，DMIC 驱动原本已开启，实际变化只有麦克风开关及随之出现的 AEC 关闭项。

使用已有真实实现：空闲的 `dmic0` 可用时宣告输入，手机协商输入后启动48 kHz 采集、单声道转换、语音处理及 PCM/Opus 加密 UDP 上行。本次未调整 Features、输出格式、modes、SETUP 响应、AAC 解码或 mixer。麦克风是否影响路由仍是待实机验证的假设。

验证结果：

- D213 `scons -j8` 完成，无编译 warning/error。ELF 包含
  `carplay_audio_input_available/start`、`rtcp_audio_capture_open/read` 及 `mic_command`。
- 独立 plistlib 比较真实 writer：mic-on 只增加 type100 的 compatibility、
  default、telephony、speechRecognition 四项输入格式，均为 `0x70004554`。Features 保持 `0x6144540280`，输出仍为相同 9 条，其余字段完全一致。
- 压缩 codec 配置下 55 项 Host 测试、boundary 和 SDK formatter/能力检查
  通过。实际 microphone worker 测试使用虚拟采集源，经 DSP、PCM/Opus 编码、加密 UDP 回环验证停止/重启。Host 测试不替代物理 DMIC 或 iPhone 验证。
- 未连接板卡，尚无实机路由/录音结果。

默认镜像已重新构建，另保存不随构建覆盖的对照文件：

```text
/home/boa/Documents/rt-carplay-artifacts/ios27-route/carplay-microphone-on.img
SHA256 c157f37eed1af69c29cef843104de71e3b1732afab6482fc926309cd58f2fce9

/home/boa/Documents/rt-carplay-artifacts/ios27-route/carplay-microphone-off.img
SHA256 318cc7fb50afc32e80ec3c62fefa55a00f9ccc69572ef30a67997b6d5994b58b
```

同目录保存 `microphone-on.config` / `microphone-off.config`。off 为第 16 节镜像的原样副本；两者均包含初始 SETUP 的功能提议日志。

实机验收：

1. 保留现有手机配对与信任，刷 on 镜像、重启并连接 Wireless CarPlay。
   检查实际 `/info` 的上述四项 `input=0x0000000070004554`；type102 media 等其余输入仍为零。确认 Features 及输出格式与 off 一致。
2. 在同一 CarPlay 音乐界面播放，观察控制中心输出选择器，保留音频
   `setup=stream_candidate` / `audio_setup` 和 `carplay_status`。目标是出现 type100/101/102 音频 SETUP，以及 streams、datagrams、packets 增长；若出现候选但协商拒绝，应按具体错误继续分析。
3. 执行 `carplay_mic` 查看输入状态。音乐播放本身不要求启动麦克风，
   输入未协商时 running=0 正常；只有请求输入后才检查采集包数和错误数。
4. 若 on 恢复音频，换回 off，保留相同配对并重复连接/播放，再回 on 复测。
   单次成功还不能证明因果，需确认差异可重复。
5. 若 on 的四项输入仍为零，先检查 `dmic0` 是否存在/被占用，本轮假设尚未
   得到有效测试。若输入非零而仍无音频 SETUP，则结果不支持缺少麦克风能力本身就是阻塞原因，继续依据实际初始 SETUP 和参考会话差异定位。


## 18. mic-on 已建立 type102：修正播放分块与增加本地音量控制

用户实测 `/info` 四项非零输入已生效，随后收到 type102 音频 SETUP，格式 `0x400000` 为 AAC-LC 44.1 kHz 双声道，收到数据且声卡启动。控制中心现在可见 CarPlay 并默认切换，喇叭已发声；声音异常且无法调整音量。这支持 mic-on 与路由恢复有关，但同配对的 off/on 重复验证尚未完成，不能断言唯一根因。该会话仍只启用 iAPChannel/viewAreas，证明至少这次不需要mainBuffered 才能建立音频。首包 6 字节为压缩载荷，日志修正为 payload_bytes。

本次修正实际播放 worker 的两个问题：

- 缓存足够时不再每块等待网络信号；按重采样后的缓存时长，仅在满 480 帧时
  渲染媒体 10 ms 块，避免包尾自动填零，并以 sound0 写入背压排队；短本地告警保持立即补零播放，避免等待后续数据。
- 在首块混音前应用已设置的音量，避免启动前静音/降音量仍输出满增益首块。

增加 `carplay_volume [0..100]` 命令用于车机侧娱乐音量，0 静音、100 unity，沿用 -45..0 dB 映射；保护性本地告警不受该音量影响。保留手机 SET_PARAMETER接口，但现有日志没有音量命令，不能承诺手机滑块/按键会发请求。保留麦克风开启，Features、格式表、AAC decoder 和声卡驱动均未调整。

55 项 ASan/UBSan Host 测试及 boundary 通过，包含 44.1 kHz 一秒输入输出精确 48000 帧、实际 worker 的 1024 帧分包长度/样值无补零、100/50/0 音量首块检查。D213 构建通过，无编译 warning/error，ELF 已包含本地音量命令和缓存时长函数。没有板卡连接，尚未听音验证修正效果。

固定镜像：

```text
/home/boa/Documents/rt-carplay-artifacts/ios27-route/carplay-audio-playback.img
SHA256 46d31ddc6f73350c0c510512e2600d3de1204ea668019ee086d9c4cc0f4c1636
```

刷入后保留 mic-on，连接无线 CarPlay 并播放同一音乐。先执行`carplay_volume 30`，再以 `carplay_volume 0` / `carplay_volume 50` 验证静音与增益变化，`carplay_volume` 查询当前值，`carplay_status` 保存音频计数。比较同一片段是否仍有断续、噪声、变调。若仍异常，需记录具体声音形态及packets/dropped bytes/write errors，再继续区分解码、供数和板端模拟输出。本镜像修正可测试的代码问题，不标记实机音质已修复。

## 19. 离线 LVGL 音频测试页：Speaker 点击声定位

用户实测第 18 节固件后 Speaker 接口仍只有“哒哒声”，未恢复正常播放。按用户要求在未连接 CarPlay 的启动页新增 **Audio tests** 入口。当前 sound0/SPK1、PA.7 低电平配置保持板级默认，尚无证据改通道/功放极性。

页面具备：

- 直接 PCM 1 kHz 测试音，以及数字静音；48/44.1 kHz、16-bit stereo，
  双声道/左/右选择。绕过网络、AAC、jitter、CarPlay mixer。
- 内置短 997 Hz AAC-LC 测试片段，通过真实 decoder 解码后直接输出 sound0；
  固定 44.1 kHz。片段源为 CC0 合成正弦，无需手机或复制音频文件。
- dmic0 麦克风输入电平、左右电平条及直接回放；固定 48 kHz，需实际麦克风。
- 测试音量 +/-、静音、开始/重启、停止、返回；显示帧数/字节/写入次数、
  输入峰值及真实错误码。测试音量默认 20%，为线性测试增益，与 CarPlay dB 音量独立。模式/采样率/声道在 Start 应用，音量和静音实时应用。

GUI 仅投递请求；接收器 owner 在开始测试前关闭 USB/iAP2/CarPlay，暂停新连接，后台测试线程独占设备。Back 必须等待测试清理完成才恢复接收器，停止超时保留测试占用而非强行重启。默认最多 10 秒，AAC 短片段更早结束。未启用 codec/麦克风的配置会禁用相应按钮。自动配置和板级 Speaker 路径未改，不依赖连接手机即可使用。

56 项 ASan/UBSan Host 测试、boundary、使用真实 SDK LVGL 头文件的 launcher生命周期回归、D213 SDK 构建通过，无编译 warning/error。测试覆盖真实后端PCM/AAC/麦克风、左右/采样率/音量/静音、设备忙、启动与写入失败、资源清理，以及先关接收器、停止超时不恢复连接。Host 使用虚拟声卡/采集；未进行实机渲染/触摸/听音验证，不标记 Speaker 点击声已修复。

固定测试镜像：

```text
/home/boa/Documents/rt-carplay-artifacts/ios27-route/carplay-audio-tests.img
SHA256 f697d9917bf7727bab4546f5f63218c738df117fa26f7c96d9e1009ccce07244
```

烧录后无需接 iPhone，在 READY 或连接失败页点 Audio tests，等待释放接收器。先 PCM tone + Both + 48 kHz + Start，比较 44.1 kHz、Left/Right，再点 PCM silence + Start，验证静音/音量，随后 AAC tone。麦克风页用说话/轻拍麦克风观察电平变化；回放从低音量开始。Back 停止测试并恢复原连接模式。

结果解释：PCM 清晰而 AAC 异常，优先查板端 AAC decoder；两种直接输出都正常而 CarPlay 异常，转查收包/jitter/mixer；直接 PCM、甚至零数据输出仍有点击声，则继续核对 DMA/时钟/声卡、功放和实际 Speaker 接口。单声道 Speaker可能只对应一个输出声道，左/右对照不能直接视为接线错误。

## 20. PCM 正常、AAC stack overflow：扩大预算并移出大型调用方缓冲

用户实测 Speaker 的 PCM Tone 正常，但 AAC 导致`thread:audio-test stack overflow`。此前 24 KiB 测试线程预算不足：旧 D213 ELF 的 test_worker 4304 字节，与嵌套的 reconstruct_channel_pair 8304、ifilter_bank 8384、faad_imdct 4240 合计下界 25232，已超过 24576 字节，还不包括中间函数。无需额外 WAV/MP3 才能复现该问题。

本轮将离线 audio-test 与 CarPlay 播放线程统一为 96 KiB，并把调用方大型PCM/压缩包/零值块/输出块从栈移到每个 worker 独占的工作区。未改第三方AAC decoder、格式、采样率、声道、功放使能和 Speaker 路由。新 ELF 离线worker 栈帧由 4304 降为 224 字节，播放 worker 由 18320 降为 784 字节。原 CarPlay 64 KiB 栈是否造成之前的点击声尚无实机证明，不能混同测试线程已确认的溢出。本轮一并增加播放路径余量并提供观察数据。

实机离线测试结束打印：

```text
[audio-test] stack total=98304 used_peak=... free_min=... bytes
[audio-test] done mode=2 frames=... bytes=... error=0
```

CarPlay 每 200 包及退出也打印栈水位。水位按 RT-Thread 的 '#' 初始化值扫描，仅覆盖已经运行的内容；不是所有输入情况下的最坏占用证明。

56 项 ASan/UBSan Host 测试、boundary 通过；新的离线 AAC 回归按 worker 请求建立 96 KiB native pthread 栈并运行真实 decoder，补足此前同步 Host 模拟未限制 RTOS 栈预算的问题。D213 构建无编译 warning/error，已确认水位日志链接进 ELF。未连接实机，待复测 AAC 是否能正常发声。

固定修正镜像：

```text
/home/boa/Documents/rt-carplay-artifacts/ios27-route/carplay-aac-stack-fix.img
SHA256 37e96411788e3dc76c35ac20d97dd72cea4150bfe6f4717b1b37941bfba07f08
```

烧录后不连接手机，先 PCM tone 44.1 kHz 对照，再 AAC tone 重复播放，保存mode=2 完成行及栈余量；无溢出且能正常发声后再连接 CarPlay 播放实际音乐。现阶段不需要新文件；若内置 AAC 成功而音乐仍异常，优先提供短 AAC-LC 与相同内容的 PCM WAV 对照，MP3 不是当前 decoder 的输入。

## 21. 离线音频正常、CarPlay 仍异常：诊断与直接输出对照

应用提交：`2bd65dc`（rt-carplay main）。

用户已确认离线测试与预期一致，CarPlay 仍异常。最新播放线程栈余量50604 字节（98304 总量/47700 峰值），当前没有 decoder 栈不足的证据。write_errors=0/auth_failures=0 不覆盖时间戳丢弃、补静音或混音问题。sink packets 为接收器累计值，transport 为当前活动流统计：从无流到新流82 datagrams，不能用 status 间隔直接计算这 82 包的速率。

本轮应用增加分阶段诊断及 `carplay_audio_path [mixer|direct]`。默认 mixer；独立 type102 立体声 direct 保留收包/解密/jitter/AAC，绕过时间戳补偿、重采样和混音，按源速率输出 sound0，保留软件音量和媒体焦点增益。有通话、Siri、其他音轨或本地提示音时回退混音。requested 是请求，last_output 是最后实际输出路径，停播时会保持；重启接收器恢复 mixer。路径切换可能短暂停音。

修正 AAC 解码零帧时仍建立时间线的边界，避免下一包人为补静音；尚无实机证据证明这是持续点击声的原因。status 增加每 lane 的解码帧/空输出/错误、PCM 峰值、负时间戳丢弃、补静音、混音丢帧、队列和耗时，输出端增加实际写入帧、峰值、设备错误、最大写入耗时。计数和峰值按接收器生命周期累计；对比差值，队列数为快照。每代流前八包还打印 payload/ts/step/frames。

验证：56 项 ASan/UBSan Host 测试及 boundary 通过，含实际 sink worker 的真实 AAC direct、源采样率/帧数、静音、切回 mixer、本地关键提示音回退及负时间戳计数回归。D213 SDK 编译通过，编译日志无 warning/error。尚未连接板端验证音质，本镜像用于隔离问题，不标记点击声已修复。

固定诊断镜像：

```text
/home/boa/Documents/rt-carplay-artifacts/ios27-route/carplay-audio-diagnostics.img
SHA256 2b959e25fce581954b1a7d1656facd48a4bc28e7f851782d4915fcbfcbde84dc
```

烧录后播放同一首音乐，保存首次 lane 日志，然后执行：

```text
carplay_audio_path mixer
carplay_status
```

继续播放 5–10 秒，再保存 status。随后执行：

```text
carplay_audio_path direct
```

当前 AAC 44.1 kHz 预期看到 playback 44100 Hz 2 ch 16-bit；继续 5–10 秒后保存 carplay_status，核对 last_output=direct。最后恢复 carplay_audio_path mixer。若 direct 正常而 mixer 异常，则用 negative_ts/silence_frames/mix_drop 等差值继续细分被绕过的三个阶段；两条路径都异常则核查 jitter、真实解码内容与设备写入节奏。当前无需提供额外 WAV/MP3 文件。

## 22. 性能日志指令开关，默认关闭

增加 `carplay_perf on` / `carplay_perf off`，无参数查询当前状态。控制周期性carplay-perf、screen-wait、USB performance/probe 报告及每 120 帧的 screen frames 消息。开机默认关闭，接收器 stop/start 保持选择，重启后恢复关闭。统计继续累计，关闭期间更新屏幕性能采样基线，避免开启后把长静默期算进一个窗口。错误、连接事件、首帧和手动 carplay_status/audio 诊断仍保留。

D213 SDK 编译通过，未进行板端串口开关验证。本镜像包含第 21 节全部音频诊断功能。

```text
/home/boa/Documents/rt-carplay-artifacts/ios27-route/carplay-audio-diag-perf-switch.img
SHA256 6fb8954d34547dfb4bb7515e540c237123409d63db3aea74696f2f77a39f7503
```

## 23. AAC 帧日志正常：修正诊断截断、补充时间戳缺口记录

用户新日志：首包零帧，随后 AAC 每包 1024 帧，timestamp 按 1024 递增；初始化/解码/负时间戳丢弃/混音丢帧/设备错误均为零。累计补静音从 6144升至 23552 帧（44.1 kHz 下总计约 534 ms），尚不能确定缺口产生在哪层。direct 请求后的 playback 44100 Hz 表明源速率输出已生效，但缺少随后status 和听感，不能宣告混音路径为根因。22323 ms 到包间隔是累计最大值，可能包含暂停，不能作为稳定播放每包延迟。

诊断行曾超过 SDK 128 字节 console 缓冲，截断 duplicate/overflow 和换行。本轮拆成短行，核对所有 audio status 格式在 32 位计数最大值时也小于128 字节；新增每代流最多八条非零时间戳缺口 expected/ts/delta/frames，不改变播放行为。保留 carplay_perf 默认关闭及全部既有音频对照命令。

实际 sink worker 的 ASan/UBSan Host 回归通过，D213 SDK 编译通过。未实机听音验证，下一步先比较 direct 生效后的听感和 status。

```text
/home/boa/Documents/rt-carplay-artifacts/ios27-route/carplay-audio-gap-diagnostics.img
SHA256 7c6148806e41ed4c97fdc3fa55c9547ff55a11961d8191e3576cd776eba8a5f2
```

## 24. direct 仍异常：修正隐藏的锁竞争丢包，增加实际音乐 PCM 回放

用户已确认 direct 下仍异常；混音、重采样或时间戳补静音不足以解释全部现象。离线与 CarPlay 使用同一 AAC decoder 配置，但实际 AAC 内容、实时供给/并发负载、音量增益不同。离线默认幅度 20%，CarPlay 未收到音量设置默认 unity；先用 carplay_volume 70（约 21% 幅度）统一实时与回放增益。

发现实际缺陷：接收回调 try-lock 失败会立即丢包，且未增加 dropped_bytes。当前 RTSP 线程优先级 15 高于音频 writer 18，抢占持锁 writer 时有此风险；本轮 Host 回归也观察到锁竞争返回 ETIMEDOUT。现给队列锁最多 2 ms 等待（当前 tick=1 ms），允许优先级继承释放短锁，超时明确计 queue_busy 和丢弃字节。锁内不解码/写设备。不能据此认定实机音质根因已被唯一确定。SDK audio.c 在 DMA 播放队列空时填零且不增加应用 write_errors，所以先前的零写入错误、零丢弃并非播放连续的充分证据。

新增 carplay_audio_probe capture/replay/stop/clear/status：writer 按需分配最多 384000 字节，录取 type102 立体声解码后、调增益前的两秒 PCM；只保留RAM，不写文件。完整或手动停止后，暂停手机音乐但保持连接，replay 可连续回放这一段数据，不等待实时网络包；共用 direct 输出和增益函数。回放期间live media 明确忽略并计 ignored_live，清空旧媒体队列/重建 decoder，通话及其他声音可抢占，末尾部分设备块通过 STOP 排空。clear 或接收器退出释放内存。两秒指样本时长，收包缓慢时会等待更久；可 stop 保留已录取的前缀。

56 项 ASan/UBSan Host 测试及 boundary 通过，覆盖短锁超时/计数及恢复接收、两秒录取上限/增益前样本、无新包回放/精确帧数、实时媒体忽略、关键提示音抢占/clear、真实 AAC 录取/回放及退出清理。Host mutex 模拟补全正超时语义。D213 SDK 编译通过，ELF 已含新命令与 queue_busy；尚未实机听音验证。

```text
/home/boa/Documents/rt-carplay-artifacts/ios27-route/carplay-audio-pcm-probe.img
SHA256 40f5f8265d98b426bb816bb5cf3011ad49844961da0e87d78b033c6a0985258d
```

烧录后保持音乐播放，执行：

```text
carplay_perf off
carplay_audio_path direct
carplay_volume 70
carplay_audio_probe capture
```

等待 probe ready（44.1 kHz 时 frames=88200），在手机暂停音乐但保持 CarPlay连接，然后 carplay_audio_probe replay，听同一段的回放。可重复 replay。若迟迟没有 ready，查询 carplay_audio_probe status，可 stop 保留部分后回放。结束使用 carplay_audio_probe clear 和 carplay_audio_path mixer。回放正常优先查实时供给/播放缓冲，回放仍异常则查实际解码数据及共用输出路径；两种结果均不单独证明某一个组件有故障。
