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

2026-11-15，在 Ubuntu 24.04/WSL、Rust/Cargo 1.100.0 及 OpenSSL 3.0.13 下执行 cargo test --locked -p livi-cp --lib，原始 LIVI 的全部 179 个测试通过，包括 15 个 info、27 个 Vehicle State、一个 UI Sync 综合测试及四个 iAP 隧道测试，覆盖元数据、配置/读/订阅/写/通知、reset/版本/布局/ACK/焦点/session/reopen 及双向封包。[选取的原始测试输出](../../../../packages/third-party/rt-carplay/docs/evidence/livi-cp-host-20261115.txt)记录精确参考版本和环境，源码与 Cargo.lock 未改动。Windows 无法编译 livi-base 的 Unix 文件权限 API，因此没有执行参考测试。命令不包含 livi-core 的 theme_catalog 测试，后者保持 NOT_RUN。这些属于参考项目 Host 结果，不是 iPhone 或开发板证据。

## SDK 里程碑与后续协议工作

[可移植基础](../../../../packages/third-party/rt-carplay/docs/zh-CN/ultra-foundation.md)提供可选 Main Display 配置、等比例显示/触摸几何、有界 OPACK 与 Vehicle State 消息处理器。由宿主指定 ID、所有权、可变性、格式/范围和回调，避免猜测车辆目录。后续 SDK 里程碑 f67b10da52a0d344d90d9e31624a7d0c2c129c19 新增内部认证双向 DataStream 传输，具备完整消息组装、同步 token 回复、有界复制队列、短写续传、对端绑定及终止清理。Make 现执行 58 个程序和 legacy E2E，CMake 通过 59 个用例，并通过 sanitizer、公共头文件、平台边界及双语文档检查。[该 SDK CI 已通过](https://github.com/boa-z/rt-carplay/actions/runs/43737988065)。

Windows GUI 与 Headless 配置均已使用 SDK f67b10d 重新构建，编译、有线配置及完整镜像全部九个组件的边界/CRC32 检查通过，原有 Classic 配置与打包输入已恢复。本里程碑没有刷写固件。

SDK 里程碑 83984a1165e1550d813d9edce6c0458bcc1c271c 新增公共 Receiver 传输注册/发送/状态 API，以及 Vehicle State 普通/高优先级和 UI Sync UUID 的 Type 130 路由，要求宿主显式注册、初始手机能力提议、完成 Pair-Verify 及加密控制连接。并行通道各自拥有独立端口、ID、方向密钥、队列及生命周期。监听失败也消耗 salt，已关闭的 salt 不能在同一会话的这些通道、屏幕、音频及 iAP 间复用。处理选择性/全部 Type 130 关闭及旧就绪事件，保留其他媒体。可移植 poll 容量为 24，可覆盖全部可选端点。边界和回调归属见 SDK 基础文档。

独立加密 Host 控制器通过公共及兼容 Receiver 配置完成三通道 TCP 交互，覆盖能力/认证门控、重复 SETUP、跨类型/媒体 salt 冲突、监听失败消耗、新 seed 重开、错误认证标签隔离，以及重连时清理未连接监听器。SDK Make、应用 Host 回归及 59/59 个 CTest 用例通过；Windows GUI/Headless 编译、有线配置及完整镜像全部九个组件边界/CRC32 检查通过。这些属于模拟传输包体，不能作为真实 Vehicle State/UI Sync 应用交互或 iPhone 证据。

SDK 路由里程碑通过了[对应 CI](https://github.com/boa-z/rt-carplay/actions/runs/43740826579)。集成版本 3a9e363d35040d9720df8d899028f78dc9c514c5 通过了 [Host 及两个固件任务](https://github.com/boa-z/luban-lite-carplay/actions/runs/43740941965)。下载的 GUI/Headless 产物通过全部 SHA256、源码身份/干净清单检查和九个完整镜像组件检查，Preview Release 任务已跳过。

D213 配置仍不注册新增应用协议，使用原有仅接收 iAP 路径。SDK 已支持将 Vehicle State 和 UI Sync 处理器显式接入 Receiver，但尚未组合对应 /info 声明。后续协议工作为由完整处理器及实际宿主目录门控的能力声明。开发开关不会增加 HEVC、displayPanels、altScreen 或未完成车辆/UI 能力。SDK 不依赖 LVGL、MPP、DE 或 D213。

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

2026-11-15 两个配置的 Windows 本地构建均通过，包括有线配置检查及完整镜像全部九个组件的边界/CRC32 检查，构建后已恢复原有 Classic 配置。首个配置里程碑的提交前镜像使用 SDK 183fd91，在 acceptance.txt 中记录未提交源码清单：GUI SHA256 为 f3d0fddcd0a66d5985bfb68740cb9c4827a2885bc56524ae40b80c2479f31dfe，Headless 为 b5d7e36f78df5ec355bcc7f428b2d68207081ed2ae6a6b5ecf91c2055613ac17，两者均未刷写。完整应用 Host 测试与真实 LVGL 9 Settings 预览通过，已发布的首个 SDK 里程碑也通过了 [GitHub CI](https://github.com/boa-z/rt-carplay/actions/runs/43734899981)。

已提交的配置里程碑 096de7eb242aba84ab868fd6b1e0f127ddd5483b 通过了[全部专用 CI 任务](https://github.com/boa-z/luban-lite-carplay/actions/runs/43736939136)：Host、GUI 和 Headless。三个产物均已下载，全部 SHA256、干净源码清单及 SDK/集成版本核对通过；下载的两个镜像也再次通过全部九个组件的边界/CRC32 检查。滚动 Preview Release 任务已跳过。这些检查确认开发产物可追溯，不能代替开发板或手机验收。

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
| L1 | PARTIAL | 显示/OPACK/Vehicle State/注册 Type 130 传输 Host 测试通过；Vehicle State/UI Sync Receiver 集成已有加密 Host 覆盖，尚缺能力声明门控。 |
| L2 | NOT_RUN | 实现并实测 D213 FULLSCREEN/FUSION/LOCAL、输入、回连及性能。 |
| L3 | NOT_RUN | 真实 iPhone Ultra 能力协商。 |
| L4 | NOT_RUN | 真实 iPhone Vehicle State 和 UI Sync 交互。 |
| L5 | NOT_RUN | 可选独立 Type 111 sink 及接收。 |

完整官方 Ultra 产品验收保持 NOT_RUN。只有发现实际协议、授权或硬件限制才记为 BLOCKED。已有 Classic 实机检查单独限定范围，不据此提升任何 Ultra 等级。

## Vehicle State 应用节点

SDK 现可通过 Receiver 配置显式接入 Vehicle State 处理器，注册两条认证通道，管理响应/通知重试、选择性通道替换及认证边界订阅清理。加密模拟控制器场景验证 config/read/write/register、HIGH 通知及 Classic 媒体共存，独立原始 UI 后端仍可使用。专门故障测试覆盖队列耗尽与额外写入前拒绝；发送状态提供待发字节、重试及放弃计数。这是 Host 应用协议证据，不代表 L3/L4 或官方 Ultra 验收。D213 应用仍未接入车辆处理器，不声明新的 Vehicle State/UI Sync 能力。

2026-11-15 验证通过：完整 SDK Make 测试、ASan/UBSan 下 CMake 61/61 测试、完整集成 Host 测试，以及原生 Windows GUI/Headless 镜像构建与九组件范围/CRC 校验。整数步长校验改用精确整数取模，消除当前 RISC-V 工具链缺少的 long-double libm 入口。SDK 版本 5c15c3699368585508d28dc284620dda988402f1 通过[对应 GitHub CI](https://github.com/boa-z/rt-carplay/actions/runs/43743788364)。本地提交前产物保留未提交源码清单，CI 生成与确定版本对应的干净产物。尚未刷写新镜像。

## UI Sync 应用节点

SDK 现有可选 UI Sync 模型与 Receiver 接入：reset/版本协商、嵌套 plist 布局/外观/焦点消息、手机选择状态、应用 ACK/noAck 及保留输出。焦点 token 由宿主提供，不采用参考固定值。独立 Python plistlib 输入及输出核对补充加密模拟控制器场景；后者使用两个实际应用处理器，覆盖队列耗尽、应用新布局前拒绝、新通道协商、错误认证标签隔离、车辆读操作继续工作及 Classic 媒体。SDK UI Sync 文档记录限制、序号假设及剩余渲染器要求。D213 仍未接入这两个处理器，不声明新的 UI/车辆能力。能力组合门控完成前 L1 保持 PARTIAL，L2–L5 保持 NOT_RUN。

2026-11-15 验证通过完整 SDK Make 测试、ASan/UBSan 下 CMake 64/64、完整集成 Host 测试，以及原生 Windows GUI/Headless 构建及全部九组件范围/CRC 校验。独立 Python 检查解析了 28 条输出 UI Sync 消息。本集成固定 SDK 版本 a76a7fe03a592a4854f15b0fcd147dfd406cb702。本地镜像保留提交前未提交源码清单，确定版本的 CI 镜像提供干净源码产物。尚未刷写新固件。

## Vehicle State 能力生成里程碑

SDK e175a8f019d5c86b5e152397fb0a75d758d11668 新增显式 Vehicle State 协商开关。/info 构造器可从接入的模型生成 vehicleStateProtocolInfo；Receiver 在创建时及每次接受新控制器前，校验完整目录和当前初值。元数据缺失、重复、不匹配或过期会拒绝启用或会话。动态宿主应在相关值变化后、重连前重新构建 /info。初始 SETUP 仅在该校验通过且手机提供对应能力时启用 vehicleStateProtocol。媒体流状态区分 registered、offered 和 enabled；enabled 不代表手机实际执行或官方验收。

独立 Python plistlib 测试验证 Unicode、代理对、内嵌 NUL、有限浮点元数据、二进制值、大于 2^53 的精确 int64，以及十种独立编码或修改的元数据情况。加密 Host 场景检查 enabledFeatures，拒绝错误能力刷新，验证重复 SETUP 保留 offer，并验证新控制器未提供协议时不启用。2026-11-15 的 SDK Make、完整集成 Host 测试及启用 ASan/UBSan 的 CMake 65/65 全部通过。集成能力测试工具显式链接新增的可移植 Vehicle/OPACK 依赖。

D213 配置仍未接入这两个处理器，negotiate_vehicle 保持 false。UI Sync 声明仍需真实显示、主题及渲染目录，不新增模拟 displayPanels、HEVC 或 Type 111。L0 为已记录的初步研究 PASS，完整需求范围的 L1 仍为 PARTIAL，L2-L5 仍为 NOT_RUN。本里程碑未刷写固件。对应提交的专用 CI GUI/Headless 镜像是构建产物，不是开发板或真实 iPhone 验收证据。
