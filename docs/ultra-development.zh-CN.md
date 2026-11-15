[English](ultra-development.md) | [简体中文](ultra-development.zh-CN.md)

# CarPlay Ultra 开发与 D213 融合

最终产品目标是完整官方 CarPlay Ultra。本开发配置是中间阶段的协议与集成验证平台。Classic 视频加 LVGL 仪表、Ultra 相关协商和完整官方产品采用不同验收标准。外观、弹窗、Host 模拟或第二路视频均不能证明完整 Ultra 支持。

## 参考来源与实现基线

开发使用 boa-z/luban-lite-carplay 的现有 carplay-oss 分支，luban-lite-jc-carplay-oss 已废弃。实现基线为集成仓库 7be401243ff9b64eb94a64cfaa082cb6dc398c27 和 SDK d0bc5190807279c62d08f78c3c428f3ca8704540；首个 SDK 基础里程碑为 183fd91167b9d2ca20e1b5f53882d3a6f1bf1ce9。保留已有 native NCM 和短 bulk-IN 修复，父仓库 gitlink 标识实际选择的 SDK 版本。

本地 LIVI 位于 C:/Project/Carplay/references/LIVI，版本为 64a05b68358db52ed887169143afd5944fd6f1f7。研究以代码和测试为依据，不直接采信 README 声明。Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI)。包含及翻译代码遵循 GPL-3.0-or-later 与 NOTICE 署名要求，保留原有声明，并在启动和 Settings 显示署名。不分发 Apple 主题、专有资产、密钥或身份材料。

## Classic、Ultra 与官方产品要求

Apple 的[架构演讲](https://developer.apple.com/videos/play/wwdc2024/10111/)描述无线体验、HEVC、Remote UI、Overlay UI、OpenGL Local UI 渲染器、需验证真实性的图像/脚本包、Punch-through UI 及携带呈现时间的 UI Sync。Classic 同样可以存在多路显示视频。[CarPlay 页面](https://developer.apple.com/carplay/)、[MFi 流程](https://mfi.apple.com/en/how-it-works)和 [Ultra 公告](https://www.apple.com/newsroom/2025/05/carplay-ultra-the-next-generation-of-carplay-begins-rolling-out-today/)提供官方背景，不能替代完整线协议或产品认证结果。

D213 已有应用融合所需的 MPP H.264 与 DE 硬件图层路径，但这不能证明其满足完整官方图形、编解码、资产及同步要求。D213 的 HEVC/OpenGL 支持、完整 Ultra 互操作及官方接入/认证尚未确认。可在继续调查这些要求的同时推进可移植协议，未完成能力保持不向手机声明。

## LIVI 源码结论

除另有说明，以下参考路径均位于 native/crates/livi-cp/src。

| 源码 | 实际行为与限制 |
| --- | --- |
| info.rs | 必选 Main Display Type 110，可选 cluster Type 111，包含物理/像素尺寸、输入、视口、安全区域和外观。hevcInfo 由配置决定。add_display_panels 可生成没有初始视频流的仪表面板，不能照搬成经过验证的单屏能力。 |
| stack/setup.rs 与 stack/control.rs | altScreen 取决于 cluster 配置；Vehicle State 取决于手机提供的能力，UI Sync 还需要主题显示配置。control.rs 会修改实际 /info 的车辆、面板及 UI Sync 信息，不能只检查 info.rs。 |
| stack/data_streams.rs | 按 clientTypeUUID 路由 Type 130：车辆普通通道 3E2F3C61-AAD0-42CB-A8AA-BF22186DA62E，高优先级 FF4A6720-F2BE-4F56-A3E1-DB3B4E37D634，UI Sync 07D9F906-8D64-4B54-A808-A20BCA2C51C2。需要 Pair-Verify 共享密钥、独立流身份/端口及双向加密隧道。 |
| iap_tunnel.rs 与 session_keys.rs | APTransportPackage 组装、双向同步确认，以及使用 DataStream-Salt 加流 seed 和 Input/Output 方向的 HKDF-SHA512 密钥。移植时需保留认证、对端绑定、nonce/key 生命周期、长度边界、分片及关闭清理。 |
| vehicle_state.rs 与 vehicle_state/wire.rs | accessory/service/characteristic/control 模型，OPACK pluginID 与嵌套 pluginData，config/read/write/register/control 交互、事务回传及已订阅变化的 updateNotify。手机写入主要约束所有权，尚不能证明普遍执行类型/范围校验，也未确认显式 unregister。 |
| ui_sync.rs | OPACK 内嵌 binary plist，包含 type/ssn/psn/asn、reset、版本协商至 2、reset 完成、command 和 ACK；支持布局/调色板/外观/焦点、错误 session 过滤及 reopen。不能据此证明完整逐帧合成器、重传队列或序号窗口/超时策略。 |
| native/crates/livi-core/src/projection/theme_catalog.rs | 解析目录/包元数据、package.json/theme.json、ZIP、显示/布局/调色板/zones 并缓存。JWT payload 提取和元数据解析不能单独证明完整资产真实性校验。 |

静态检查发现 info.rs 有 15 个测试标记，vehicle_state/tests.rs 有 27 个，ui_sync.rs 有一个综合测试，覆盖元数据、配置/读/订阅/写/通知，以及 reset/版本/布局/ACK/焦点/session/reopen，另有隧道与合成主题测试。此处未执行 LIVI 测试，测试存在不代表 iPhone 或开发板实测通过。

## SDK 里程碑与后续协议工作

[可移植基础](../../../../packages/third-party/rt-carplay/docs/zh-CN/ultra-foundation.md)提供可选 Main Display 配置、等比例显示/触摸几何、有界 OPACK 与 Vehicle State 消息处理器。由宿主指定 ID、所有权、可变性、格式/范围和回调，避免猜测车辆目录。Make 执行 57 个程序和 legacy E2E，CMake 执行 58 个用例，并通过 sanitizer、公共头文件、平台边界及双语文档检查。

实际 Type 130 仍是原有仅接收 iAP 路径。新 Vehicle State 处理器尚未接入 Receiver 协商及 /info，UI Sync 尚未实现。后续为有界双向注册、认证后的完整消息路由、手机能力门控、生命周期/队列失败测试、UI Sync binary-plist 状态/ACK，以及公共 Receiver 状态和错误接口。开发开关不会增加 HEVC、displayPanels、altScreen 或未完成车辆/UI 能力。SDK 不依赖 LVGL、MPP、DE 或 D213。

## 专用开发配置与产物

| 配置 | 文件 | 当前用途 |
| --- | --- | --- |
| GUI | d21x_demo128-nand_rt-thread_rtcarplay_ultra_gui_defconfig | 原有 LVGL launcher 和 Classic 媒体，明确的 Ultra 开发开关及署名，融合尚待实现。 |
| Headless | d21x_demo128-nand_rt-thread_rtcarplay_ultra_headless_defconfig | 不带 LVGL 的自动 Classic 连接，用于协议及生命周期验证。 |

两者保留独占 native NCM、n0 身份和关闭诊断跟踪的设置。开发开关应用显式默认 Main Display 配置，不声明未实现协议。每次构建先验证匹配 bootloader，再检查有线配置及完整镜像。脚本退出时恢复共享生成配置和 bootloader 打包输入。Windows 使用现有原生工具链，不能通过 WSL 构建替换为 Linux 工具链。

```sh
bash .ci/build-ultra-demo.sh gui
bash .ci/build-ultra-demo.sh headless
```

ultra-demo workflow 在 SDK 和应用 Host 检查后构建两个变体。可下载的 Actions 产物在 output/ultra-ci/gui 或 output/ultra-ci/headless 中包含完整 IMG、ELF/map、bootloader、解析后的配置、源码/验收元数据、日志和 SHA256SUMS。这些是开发镜像，不是经过认证的 Ultra Release。推送里程碑使用提交标记 [ultra-dev]，使现有滚动 Classic Preview Release 任务跳过；专用构建及产物工作流仍执行。普通 Classic 提交保留原有发布行为。

2026-11-15 两个配置的 Windows 本地构建均通过，包括有线配置检查及完整镜像全部九个组件的边界/CRC32 检查，构建后已恢复原有 Classic 配置。提交前镜像在 acceptance.txt 中记录未提交源码清单；Actions 产物标识其已提交源码。GUI 镜像 SHA256 为 f3d0fddcd0a66d5985bfb68740cb9c4827a2885bc56524ae40b80c2479f31dfe，Headless 为 b5d7e36f78df5ec355bcc7f428b2d68207081ed2ae6a6b5ecf91c2055613ac17，两者均未刷写。完整应用 Host 测试与真实 LVGL 9 Settings 预览通过，已发布的 SDK 里程碑也通过了 [GitHub CI](https://github.com/boa-z/rt-carplay/actions/runs/43734899981)。

## 单屏融合实施方案

FULLSCREEN 保留原有 Main Display。FUSION 将 MPP 视频直接由 DE 缩放到有界窗口，同时保留运行中的 ARGB LVGL 仪表层。LOCAL 在没有视频时继续显示速度、挡位、电量及连接状态。初始数据必须标记 SIMULATED，预留真实数据提供接口。该应用融合独立于官方 Ultra Local UI。

现有 video_layer_policy 在首帧后隐藏整个 UI，launcher 在视频活动时停止页面刷新，需要改为模式相关策略。视频已通过 MPP buffer、DE scanout 和 VSYNC 显示，应保留硬件合成，正确处理更新失败及关闭时的缓冲区归属，避免逐帧软件拷贝。触摸使用同一份实际缩放矩形，拒绝黑边，跨区域时保留从按下到抬起的归属，模式/会话改变时取消触点。LVGL 调用只在自身线程执行。断连、重连及视频停滞时本地仪表保持运行。阶段耗时、帧率、CPU、内存计数保持测量含义，接收端处理耗时不等于手机到屏幕端到端延迟。

## 开发板准备与证据

2026-11-15 开发板通过 COM9、115200 响应命令，已验证 aiburn 服务及 aicupg 软件进入升级模式，服务识别到一个升级 USB 设备。不刷写退出该模式需要用户断电重启；本次检查未写入或擦除固件。仅在镜像和备份全部就绪后进入升级模式。

现有固件的 Classic 有线会话在 65,093 ms 视频间隔内新增 1,169 帧，丢帧/错误/重置为零，队列峰值为一；native NCM 的 RX/TX/input 错误为零。用户确认画面流畅、触摸及声音正常，日志记录到音频启动时一条 gap。这是有限的当前固件观察，尚未建立已安装镜像的 hash/源码版本，不能验证本次新增源码、物理拔插或 Ultra。

接收端 seed、AirPlay 记录和 USB 信任文件已备份到 Git 外限制访问的目录，两次独立 /data 读取 SHA256 一致，共六个文件、51,252 字节，临时文件推送再读回也一致。备份没有 bt_settings，蓝牙 bond 持久化尚未确认。新版 Android platform-tools 配合 ADB_LIBUSB=1 可识别 CherryADB，SDK 自带旧版不能。新版客户端传输完成后仍保持运行，需单独结束，因此除检查传输完成输出，还核对了文件 hash。仓库不包含配对字节、手机编号或私有清单。后续完整刷写前需重新检查文件清单及备份稳定性，刷写后恢复所需身份并验证自动回连。

## 验收检查点

| 等级 | 状态 | 仍需证据 |
| --- | --- | --- |
| L0 | 初步源码研究 PASS | 本文区分代码、未运行参考测试和官方产品要求，后续协议研究继续。 |
| L1 | PARTIAL | 显示/OPACK/Vehicle State Host 测试通过，尚缺实际双向传输、协商接入和 UI Sync。 |
| L2 | NOT_RUN | 实现并实测 D213 FULLSCREEN/FUSION/LOCAL、输入、回连及性能。 |
| L3 | NOT_RUN | 真实 iPhone Ultra 能力协商。 |
| L4 | NOT_RUN | 真实 iPhone Vehicle State 和 UI Sync 交互。 |
| L5 | NOT_RUN | 可选独立 Type 111 sink 及接收。 |

完整官方 Ultra 产品验收保持 NOT_RUN。只有发现实际协议、授权或硬件限制才记为 BLOCKED。已有 Classic 实机检查单独限定范围，不据此提升任何 Ultra 等级。
