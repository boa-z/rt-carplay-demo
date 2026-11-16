[English](ultra-development.md) | [简体中文](ultra-development.zh-CN.md)

# CarPlay Ultra 开发与 D213 融合

当前应用里程碑：专用 Ultra GUI/Headless 配置已显式启用 [SIMULATED 发动机 Vehicle State 协议实验入口](vehicle-protocol-demo.zh-CN.md)，普通 Classic 配置保持不变。下文各节记录对应历史里程碑，其中“D213 尚未接入 Vehicle 处理器”的描述早于此次实验集成。D213 的 UI Sync 及其渲染器/资产/呈现能力仍保持关闭。

最新实机检查点：真实手机已提供并启用 Vehicle State，两条通道连接，处理器收到 1 条请求；用户确认 FULLSCREEN 视频正常，运行时改为逐像素 alpha 后 FUSION 左侧视频恢复。应用现对 FUSION 使用逐像素 alpha，避免空白 UI 区域遮住视频。[确切镜像、观察和限制](evidence/d213-vehicle-controls-flash-20261115.md)记录全部证据。L2–L4 根据有限实测记为 PARTIAL，固化修复的新镜像、输入/重连、完整协议交互及官方 Ultra 仍未完成；下文历史 NOT_RUN 属于较早里程碑。

用户于 2026-11-15 调整范围：当前开发环境不再要求完整官方 CarPlay Ultra。目标是可移植 Ultra 协议研究与基础能力，以及实用的 D213 单屏融合 Demo。官方渲染器/资产、HEVC/OpenGL 集成和产品接入/认证不属于当前验收范围，也不阻塞当前目标。Classic 视频加 LVGL 仪表、Ultra 相关协商和完整官方产品采用不同验收标准。外观、弹窗、Host 模拟或第二路视频均不能证明完整 Ultra 支持。

当前硬件判断：D213 的 MPP/DE/LVGL 路径继续用于融合显示和协议验证。Apple [公开架构](https://developer.apple.com/videos/play/wwdc2024/10111/)明确描述基于 OpenGL 的 Local UI 渲染器、专用 OpenGL 合成器和必需的 HEVC 视频能力。当前 D213 集成没有已实现并验证的 OpenGL 后端、官方资产/呈现渲染器及 HEVC 媒体路径，GE2D/DE 二维合成和 LVGL 仪表不能证明具备这些能力。完整官方 Ultra 需要合适的图形/编解码平台，以及相应软件、资产、访问权限和认证；未来官方产品所需的平台调查属于当前目标之外的独立工作。用户照片确认 8ac6f504 融合镜像没有上下黑边；压缩感、触摸、重连及官方验收须分别验证。

## 当前范围与验收

本节取代下文历史记录中将完整官方产品作为目标或 L1 阻塞条件的描述。2026-11-15 用户确认“上一个版本我已经验证了，fusion模式的效果可以”。未提供具体镜像/提交，故仅记录为用户反馈的视觉确认，不能绑定最新镜像，也不能推导触摸、持久化、重连或完整协议验收通过。[最新构建检查点](evidence/d213-persistent-fullscreen-20261115.zh-CN.md)保留准确运行时/镜像身份及待验证项目。

| 等级 | 当前状态 | 证据边界 |
| --- | --- | --- |
| L0 | PASS | 已记录 LIVI 源码/测试研究、公开架构对比及实现差距报告。 |
| L1 | 已实现基础范围 PASS | 可移植显示、Vehicle State、认证 DataStream 和 UI Sync 基础有 Host 检查记录，仅验收已实现基础，不代表完整官方协议覆盖或手机互操作。最新集成 Host、GUI、Headless CI 通过。 |
| L2 | PARTIAL | 用户确认上一版本 Fusion 视觉效果。准确镜像关联、最新全屏/设置行为、触摸、生命周期和受控资源/延迟测量尚未完成。 |
| L3 | PARTIAL | 历史真实手机提供/启用 Vehicle State，Normal/HIGH 通道连接；UI Sync 及剩余协商检查为 NOT_RUN。 |
| L4 | PARTIAL | 历史真实手机请求被处理、响应入队；请求方法/手机接受、读写/订阅/通知语义及 UI Sync 实机交互仍为 NOT_RUN。 |
| L5 | 真实手机 NOT_RUN；可选 | 已有独立 Type 111 Host 传输/Null Sink；真实手机协商及独立 Cluster 接收未验证。 |

优先完善已实现的单屏体验和有界 Vehicle State 互操作，UI Sync 在实际宿主契约就绪后再推进。未实现的手机侧能力继续关闭。实机由用户手动测试，本次范围调整不授权连接或刷写开发板。完整官方 Ultra 属于范围外事项，不再作为本目标的 BLOCKED 项。

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

2026-11-15，在 Ubuntu 24.04/WSL、Rust/Cargo 1.100.0 及 OpenSSL 3.0.13 下执行 cargo test --locked -p livi-cp --lib，原始 LIVI 的全部 179 个测试通过，包括 15 个 info、27 个 Vehicle State、一个 UI Sync 综合测试及四个 iAP 隧道测试，覆盖元数据、配置/读/订阅/写/通知、reset/版本/布局/ACK/焦点/session/reopen 及双向封包。[选取的原始测试输出](../../../../packages/third-party/rt-carplay/docs/evidence/livi-cp-host-20261115.txt)记录精确参考版本和环境，源码与 Cargo.lock 未改动。Windows 无法编译 livi-base 的 Unix 文件权限 API，因此没有执行参考测试。后续独立命令通过六个 livi-core theme_catalog 测试及一个 livi-ui-model 仪表布局测试，见[补充源码研究](../../../../packages/third-party/rt-carplay/docs/zh-CN/livi-theme-review.md)及[原始输出](../../../../packages/third-party/rt-carplay/docs/evidence/livi-theme-host-20261115.txt)。该模型对未知厂商布局采用通用仪表回退，元数据选择不能证明完整官方主题渲染。这些属于参考项目 Host 结果，不是 iPhone 或开发板证据。

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

## 单屏融合实现

Ultra GUI 配置现已通过 DE 视频位置与 UI 图层裁剪实现 FULLSCREEN/FUSION/LOCAL，包含明确标注的模拟仪表、复制式车辆数据接口、共用已锁存几何的触摸映射、安全的扫描帧生命周期和接收端阶段统计。操作、约束、Host 覆盖和实机验收清单见[单屏融合文档](single-screen-fusion.zh-CN.md)。2026-11-15 原生 GUI/Headless 构建及实际 LVGL Host 预览通过，L2 在确切镜像实机验证前保持 NOT_RUN。该应用融合独立于官方 Ultra Local UI。

## 开发板准备与证据

2026-11-15 开发板通过 COM9、115200 响应命令，已验证 aiburn 服务及 aicupg 软件进入升级模式，服务识别到一个升级 USB 设备。不刷写退出该模式需要用户断电重启；本次检查未写入或擦除固件。仅在镜像和备份全部就绪后进入升级模式。

现有固件的 Classic 有线会话在 65,093 ms 视频间隔内新增 1,169 帧，丢帧/错误/重置为零，队列峰值为一；native NCM 的 RX/TX/input 错误为零。用户确认画面流畅、触摸及声音正常，日志记录到音频启动时一条 gap。这是有限的当前固件观察，尚未建立已安装镜像的 hash/源码版本，不能验证本次新增源码、物理拔插或 Ultra。

接收端 seed、AirPlay 记录和 USB 信任文件已备份到 Git 外限制访问的目录，两次独立 /data 读取 SHA256 一致，共六个文件、51,252 字节，临时文件推送再读回也一致。备份没有 bt_settings，蓝牙 bond 持久化尚未确认。新版 Android platform-tools 配合 ADB_LIBUSB=1 可识别 CherryADB，SDK 自带旧版不能。新版客户端传输完成后仍保持运行，需单独结束，因此除检查传输完成输出，还核对了文件 hash。仓库不包含配对字节、手机编号或私有清单。后续完整刷写前需重新检查文件清单及备份稳定性，刷写后恢复所需身份并验证自动回连。

## 验收检查点

| 等级 | 状态 | 仍需证据 |
| --- | --- | --- |
| L0 | 初步源码研究 PASS | 本文区分代码、未运行参考测试和官方产品要求，后续协议研究继续。 |
| L1 | PARTIAL | 显示/OPACK/Vehicle State/注册 Type 130 传输 Host 测试通过；Vehicle State/UI Sync Receiver 集成已有加密 Host 覆盖，尚缺能力声明门控。 |
| L2 | NOT_RUN | 实测已实现的 D213 FULLSCREEN/FUSION/LOCAL、输入、回连及性能。 |
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

## 融合模式切换前的 DE 扫描帧生命周期

视频 Sink 在图层更新或 VSYNC 失败时，保留上一张显示帧及最多一张待确认替换帧。在获取更多解码帧前先重试该替换帧，即使没有新输出也会重试。只有确认更新与 VSYNC 成功，才转移扫描帧归属并将旧帧归还 MPP。显示阶段耗时包含这些重试。仍使用现有硬件图层链路，不新增逐帧图像拷贝。

解码恢复和停止均先关闭 DE 视频层并等待 VSYNC，再归还扫描帧及销毁解码器。解码工作线程退出时，将扫描帧留给完成线程等待的生命周期所有者。关闭图层或 VSYNC 失败时，stop 返回 RTCP_EIO，并保留解码器、帧及同步资源；调用者必须重试停止，成功后才能重启或更换观察者。现有应用关闭所有者已对停止失败进行重试。工作线程退出超时仍按原有约定保留资源。

实际源码 Host 测试模拟扫描帧归属不确定的情况，并断言硬件仍可能读取帧时不得销毁解码器。覆盖替换失败与重试、恢复失败、图层关闭失败、停止等待失败、保留资源后重试、观察者顺序，以及既有包队列、恢复及重复生命周期。2026-11-15 的完整集成 Host 测试及 Windows 原生 Ultra GUI/Headless 构建通过，两种镜像的九个组件均通过边界与 CRC 校验。真实 D213 故障注入及融合显示验收仍为 NOT_RUN。本项前置修复尚未实现 FULLSCREEN/FUSION/LOCAL 切换或官方 Ultra 渲染。

## UI Sync 目录复制里程碑

SDK 27de06565aadb460fb10520812b4ebf8c3521209 增加可选的宿主目录复制：主题/面板/显示标识、像素/物理尺寸，以及支持的布局和各自调色板、区域/组件候选。不支持的宿主或手机选择会在状态及 ACK 提交前拒绝，目录跨通道重置保留。Receiver 使用独立 last_rejection 保存具体拒绝原因，避免被后续通道关闭的通用错误覆盖。这是后续真实宿主能力组合的便携契约，不是渲染器，也不会隐式声明 uiSync/displayPanels。

SDK Make、ASan/UBSan 下 CMake 65/65 及完整集成 Host 测试通过。Python 独立消息生成/解析覆盖 35 条 UI Sync 输出；加密 Receiver 测试仅关闭收到未知布局的 UI 通道，车辆读取继续，具体原因保留，重开后接受支持的选择。D213 仍未绑定目录或 UI Sync 处理器，不声明新的 UI 能力，既有融合配置未改变。

原版 LIVI 的追加测试通过六个主题目录测试及一个仪表布局测试，使用模拟 JSON/ZIP/目录和本地缓存，没有抓取或复制 Apple 主题资产。参考代码提取 ID、调色板和地图区域，未知厂商布局回退到通用仪表，不能证明完整官方 Local UI 渲染。SDK 中英双语报告记录了源码与测试的确切边界。

当前板端仍为此前刷入的 1b96d3f8，不是本次 SDK 版本。再次通过 COM9 观察到请求 FUSION、实际 LOCAL、video=0、显示错误为零，USB 已在 CarPlay 配置中连接，但 Lockdown 仍在收到视频前建连失败。私有配对文件已核对恢复，不能据此推定自动回连成功。L1 仍为 PARTIAL，完整 L2 及 L3-L5 均保持 NOT_RUN。本里程碑没有再次刷写固件。

最初的目录里程碑 CI 在双语文档检查处停止：新研究文档的语言导航及源码引用未匹配。SDK 284683f55809718ed83713797ea95f29857f9568 修正文档后，本地完整 Make 检查（包含文档门禁）再次通过。集成工程现固定到该修正版，开发板固件和手机验收状态没有因此变化。

SDK 3d10a1e1f3b141acdbcfb570ebea49ab10817507 为手机布局/调色板的四个标识字段补充独立内嵌 NUL 消息。现有解析以 EINVAL 拒绝完整字符串，保持手机选择及序号/ACK 状态不变且没有输出。本地完整 SDK 检查通过，包含 35 条独立解码的 UI Sync 输出。这是拒绝路径覆盖补充，没有修改协议实现。

[D213 编解码/图形研究](d213-ultra-feasibility.zh-CN.md)记录了实际 MPP 工厂/构建与 GE 接口、公开架构要求及待确认的硅片/运行时条件。当前路径没有 HEVC 解码入口，但不能据此证明硅片无法通过其他厂商实现支持。

## 目录里程碑 CI 修正与产物核验

SDK 3d10a1e1f3b141acdbcfb570ebea49ab10817507 的[两项 SDK CI 均通过](https://github.com/boa-z/rt-carplay/actions/runs/43758862128)。集成 538a7a8fa8fcda1de1c4c3a91a8fd327dcce21cf 的 [Host、GUI、Headless CI 均通过](https://github.com/boa-z/luban-lite-carplay/actions/runs/43758941951)。成功运行已取代最初失败的双语文档门禁结果，原失败不是 sanitizer 或线协议失败。

三个集成产物均已下载。两份固件清单的集成/SDK 版本与上述版本完全一致，源码工作区记录均为空。每份固件产物的六项 SHA256 全部通过，每份完整镜像的九个组件边界/CRC32 检查通过。GUI 镜像 SHA256：3d055d00f507cb2ae290bf1a96652702577121b4889e761361a46c79d7b5e901。Headless 镜像 SHA256：0b9bcdcfd56e413d7ce1aee8c8139f3605cef28170907c11707f5b2ade5a991f。后续仅文档提交不会改变这些产物身份。

本次下载镜像尚未刷写，已连接开发板仍运行先前记录的 1b96d3f8 镜像，没有新增自动重连、物理融合/触摸或真实 iPhone Ultra 验收结论。L0 源码研究保持 PASS，完整协议范围的 L1 仍为 PARTIAL，L2-L5 仍为 NOT_RUN。滚动 Preview Release 已跳过，本批里程碑没有发布 Release。

## 单 Main Display UI Sync 实验能力门控

SDK a25c0cb99553492bc6d65d5f84fddceb25cfebe6 增加[显式 UI Sync 实验能力协商](../../../../packages/third-party/rt-carplay/docs/zh-CN/ui-sync-capabilities.md)。/info 生成器仅在已绑定复制目录，且显示 UUID、像素/物理尺寸与 Main Display 一致时接受模型，输出空 uiSyncInfo 和唯一真实 displayPanels 项，引用原点为零的单个 Type 110 流，不新增虚拟仪表、显示插件、Type 111、altScreen 或 HEVC。

Receiver 的 negotiate_ui_sync 独立检查接入处理器、已绑定目录、匹配的初始/刷新元数据及手机提议，然后才加入 enabledFeatures。默认 Classic 响应字节不变。刷新 UI 元数据无效时，即使 Vehicle 元数据仍然有效，也拒绝新控制器。不可变 /info 快照可在 SETUP 前提供显式 UI 元数据，而 LIVI 的 uiSyncInfo 在接受提议后按条件提供；这条窄范围实验路径的真实手机接受情况仍为 NOT_RUN。宿主需实现真实资产/渲染器/呈现契约，接入期间目录保持不可变；元数据门控不验证这些实现。

完整 SDK Make 检查通过，CMake 在 ASan/UBSan 下通过 66/66 项。独立 plistlib 覆盖 65 项元数据及四种 Vehicle/UI SETUP 能力组合。加密 Receiver 场景验证已启用能力、UI/Vehicle 交互、仅刷新 UI 元数据无效时的拒绝、重连/无提议状态和不变的 Classic 媒体。在手工链接的能力测试中补齐真实 UI 模型/值写入器源码后，完整集成 Host 套件通过。该测试还确认现有各 D213 能力配置均保持单 Type 110，且没有 UI/HEVC/面板声明。

D213 仍未接入这两个处理器，两个协商开关均未启用。LVGL 融合仪表不满足渲染器/资产/呈现契约，仍为独立应用演示。本里程碑未刷写固件。L0 保持 PASS，完整范围的 L1 为 PARTIAL，L2-L5 为 NOT_RUN。

## 单 Main UI Sync CI 与开发板观察

SDK a25c0cb99553492bc6d65d5f84fddceb25cfebe6 的 [sanitizer Host 和双语文档 CI 均通过](https://github.com/boa-z/rt-carplay/actions/runs/43761277096)。集成 2808437f848c046a1f90798f72023cffb989f297 的 [Host、GUI、Headless CI 均通过](https://github.com/boa-z/luban-lite-carplay/actions/runs/43761359152)。三个产物均已下载，两份固件验收清单的精确版本匹配上述记录，SDK/集成工作区记录为空。每份固件产物六项 SHA256 及每份完整镜像九个组件边界/CRC32 检查全部通过。

GUI 镜像 SHA256：3cc1343e289e5134fab37542400402ad98bef5f0759b4f774574a8a4ce5ee01d。Headless 镜像 SHA256：7b1075016ad1ea0161546d2bd282ed99369781a27958987c63443f1ceb5a1b91。两份镜像均未刷写，滚动 Preview Release 已跳过。后续仅文档提交不会改变这些镜像的源码身份。

2026-11-15 使用项目 aiburn 技能经 COM9 只读查询，返回 requested=FUSION、effective=LOCAL、generation=3、latched=0、video=0、error=0，以及 SIMULATED 车辆样本。连接管理器报告 owner=wireless、stage=1、usb_ready=1、attached=1、bt_ready=1。这些是现有固件在查询时的协调器/连接状态，不能证明可视合成、自动重连或连续正常运行。没有重启、进入升级模式或传输身份文件。原始日志保留在 Git 外的受限私有设备目录，此处没有手机标识或配对材料。物理融合/输入与真实 iPhone Ultra 验收仍为 NOT_RUN。

## 按顺序将 UI Sync 选择交给宿主渲染器

SDK d6ab43aef99f39c31bcffaab317b0352cef35136 增加可选 queue_phone_choices 和复制 peek/consume API。十六项 FIFO 保留布局、调色板、外观的顺序、源 session/sequence 及本地重置 generation。布局加调色板必须原子占用两个空槽。默认保留原有最终状态行为，不分配选择队列工作区。本地交接不会增加手机侧能力、渲染器、资产运行时或呈现时钟。

队列满时，在状态/ACK 提交前拒绝当前命令，保留旧记录。成功协议/通道重置会统计放弃记录、递增 generation，宿主应作废已复制的旧工作。消费记录仅代表宿主接受，不能证明已经绘制或呈现。[SDK 状态与归属文档](../../../../packages/third-party/rt-carplay/docs/zh-CN/ui-sync.md)说明生命周期、noAck、失败及重置契约。该接口参考 LIVI 分开的手机选择，并补充明确上限和生命周期处理。

本地完整 SDK Make、ASan/UBSan 下 CMake 66/66 项及完整集成 Host 套件均通过。覆盖输入/输出复制生命周期、顺序、noAck、旧/重复消息过滤、双项拒绝、环形回绕/重试、失败 RESET 事务性及放弃计数。加密 Receiver 测试消费实际选择、区分重开 generation，在传输持续排空时填满宿主 FIFO，仅拒绝 UI 下一条命令并继续 Vehicle 读取。D213 仍未接入 UI 处理器，不声明 UI Sync；本次未刷写。L0 保持 PASS，完整范围 L1 为 PARTIAL，L2-L5 为 NOT_RUN。

## 有序 UI Sync 交接 CI 与镜像核验

SDK d6ab43aef99f39c31bcffaab317b0352cef35136 的 [sanitizer Host 和双语文档 CI 均通过](https://github.com/boa-z/rt-carplay/actions/runs/43763712003)。集成 2f5f5895d019503c600361aa573bede2e1f6196b 的 [Host、GUI、Headless CI 均通过](https://github.com/boa-z/luban-lite-carplay/actions/runs/43763767218)。三个产物均已下载，两份固件验收清单的精确版本匹配上述记录，SDK/集成工作区记录为空。每份固件产物六项 SHA256 及每份完整镜像九个组件边界/CRC32 检查全部通过。

GUI 镜像 SHA256：324226e2a572111d7943e743227960fecc10bc1097c4e64c2321999f29a31971。Headless 镜像 SHA256：9d938362522e9cc57ae81431045d886cc22d65fdf33e4658ddc87c0653d40b47。两份镜像均未刷写，滚动 Preview Release 已跳过；后续仅文档提交不会改变这些镜像身份。队列提供宿主交接基础，不能证明渲染器或物理呈现已完成。L0 保持 PASS，完整范围 L1 为 PARTIAL，L2-L5 为 NOT_RUN。

## 应用 Vehicle State 实验

专用 Ultra 配置接入车辆侧拥有的三项内燃机模拟模型，声明匹配的 /info 目录，并显式选择手机提议的 vehicleStateProtocol。应用在 Receiver 所有者上更新模型，每次新控制器接入前刷新当前初值，先销毁 Receiver 再释放模型。carplay_status 发布复制后的模拟/协议/交付观察值。不会从发动机模型推断车速、挡位、电池 SOC、UI Sync、显示面板、HEVC 或备用视频流，参见[模型、源码映射及实机步骤](vehicle-protocol-demo.zh-CN.md)。

本地完整集成 Host 套件通过，包含开启/关闭选项的实际应用启停、元数据刷新、处理器生命周期、订阅/HIGH 通知、拒绝写入，以及独立 plistlib 对目录和缺省未支持声明的验证。专用镜像 CI 检查两份固件 ELF 中实际模型/协议符号，并在验收清单记录模拟数据源。本里程碑没有新增开发板或手机验收，L0 PASS、完整范围 L1 PARTIAL、L2-L5 NOT_RUN 不变。

## Vehicle State 应用 CI 与设备准备

集成 aa4b325ee1dd23ee39d1a5f2b3c2ad94a07660dc，沿用 SDK d6ab43aef99f39c31bcffaab317b0352cef35136，[Host、GUI、Headless CI 全部通过](https://github.com/boa-z/luban-lite-carplay/actions/runs/43765510252)。三个产物均已下载。两份固件清单精确匹配上述版本，SDK/集成工作区记录为空，并标明 SIMULATED_ENGINE_RPM、PHONE_OFFER_REQUIRED 及 UI Sync 关闭。每份固件六项 SHA256、完整镜像九个组件边界/CRC32 检查全部通过。下载后的 ELF/config 组合也在本地通过有线配置检查器，包含实际模拟模型和协议符号。

GUI 镜像 SHA256：0b4f74869c5a7d5beaf5ce8ba34d5cf40877d32376f217c1d8a68c47f3c5d269。Headless 镜像 SHA256：3b2927be7ffb8d8d15822985d5e61392944c23823f651b3b84d5daf8ea5b1b1a。没有发布 Release，滚动固件工作流已跳过；随后的仅文档提交不会改变这些镜像身份。

2026-11-15 设备准备发现一个可用 ADB 文件同步设备。两次新的独立 /data 回读均得到 6 个身份/配对文件，共 51,252 字节，各文件 SHA256 相互一致，也与先前已验证备份一致。原始文件、清单和日志保留在 Git 外的受限私有设备目录。传输完成后仍未退出的客户端分别终止，没有停止 ADB 服务或串口 monitor。备份没有蓝牙 settings 文件，因此不能证明 bond 持久化或自动回连。

aiburn 串口命令返回成功，但没有提供运行输出，另一次 readlog 返回没有可用串口日志，故本检查点尚未确认 Shell 可达。未执行 aicupg、刷写、重启或恢复，模拟模型尚未在 D213 上运行，开发板保留先前记录的固件。L0 PASS、完整范围 L1 PARTIAL、L2-L5 NOT_RUN 不变；进入升级模式前需要确认调试通道，设备状态变化后应重新备份。

## Main/备用视频独立会话基础

SDK 8a0d71acb97bbc62426bc96ae13872f2baa2b95d 通过显式配对的内部视频回调分离 Type 110 与实验性 Type 111 状态，覆盖混合 SETUP、独立端口、按槽位替换/清理、过期清理过滤、共享连接 ID 预留及事务回滚。缺省生产 Receiver/server 仍使用原有 Main 监听，不接入新回调、不声明第二显示或启用 altScreen。[SDK 契约及剩余阶段](../../../../packages/third-party/rt-carplay/docs/zh-CN/video-stream-session.md)明确区分本控制面基础与第二个加密监听、媒体 Sink 及 L5 接收。

本地完整 SDK Make、ASan/UBSan 下 CMake 67/67 项及完整集成 Host 套件通过。Python plistlib 独立提供混合流输入，并解码不同的 Main/备用响应端口、未改变的初始能力及旧配置中省略的不支持备用流。回调测试覆盖重复/冲突 ID、幂等、失败/零端口回调、音频及响应编码失败、回滚和单次资源清理。这不是加密 Type 111 或真实手机证据，D213 的模拟 Vehicle State 配置及独立 LVGL 融合行为不变。

后续设备检查确认 COM9 已注册且串口写入被接受，但仍没有运行输出，升级接口也未枚举到升级设备。这些观察不能证明 Shell 可达或确定静默原因。未尝试进入升级模式、刷写或重启，L0 PASS、完整范围 L1 PARTIAL、L2-L5 NOT_RUN 不变。

## 独立视频会话 CI 与产物核验

SDK 8a0d71acb97bbc62426bc96ae13872f2baa2b95d 的 [sanitizer Host 和双语文档 CI 均通过](https://github.com/boa-z/rt-carplay/actions/runs/43768403492)。集成 5be76b8b9b322481d9d40bc1a6839c9f3da1ff7f 的 [Host、GUI、Headless CI 全部通过](https://github.com/boa-z/luban-lite-carplay/actions/runs/43768478380)。三个产物均已下载，两份固件清单精确匹配上述版本，SDK/集成工作区记录为空。每份固件六项 SHA256、完整镜像九个组件边界/CRC32 检查全部通过。

GUI 镜像 SHA256：fc31e995d7e2d793019031f535343c281297de16adaaa8e5379f259c92defee1。Headless 镜像 SHA256：1fc2a4e97790d4486b6b872d13568a40b9e4cdffdd01cf7bf5d3a8690bf93e0f。下载后的 ELF/config 组合在本地通过有线/模拟模型检查器，清单保留 SIMULATED_ENGINE_RPM、PHONE_OFFER_REQUIRED 及 UI Sync 关闭。生产 server 仍未接入实验性备用视频回调。两份镜像均未刷写，滚动 Preview Release 已跳过；仅文档跟进不会改变镜像身份及 L0 PASS、完整范围 L1 PARTIAL、L2-L5 NOT_RUN 的验收状态。

## 实验性独立备用视频传输

SDK 344ce0c2a03dd545961bb11ccaa2d2805efa6954 新增公共 Receiver 显式 experimental_alt_video 开关、独立 Type 111 H.264 TCP 监听、按连接派生的密钥/nonce 周期、独立媒体回调或明确的 Null Sink，以及所有者线程状态/统计查询。备用套接字/认证失败只退役自身端点，共享已消费 ID 台账继续保留；Main 丢失保持完整控制器清理。默认 Classic 路径和专用 D213 Ultra 配置均保持开关关闭，不声明第二显示 /info、altScreen、HEVC、第二路硬解或官方渲染能力。[SDK 契约](../../../../packages/third-party/rt-carplay/docs/zh-CN/video-stream-session.md) 说明接口和剩余能力门控。

本地完整 SDK Make、启用 RTCP_SANITIZE 的 CMake 69/69 项和完整集成 Host 套件通过。新增公共 Receiver 场景使用真实 Pair-Setup/Pair-Verify、加密 RTSP 及分别加密且交错发送的 Main/备用 TCP 帧，覆盖认证/初始 SETUP 门控、独立端口/Sink/计数器、Null Sink、ID 复用拒绝、过期清理、监听失败、仅错误就绪时避免阻塞 accept/recv、错误密钥/认证标签/EOF 隔离、新 ID 恢复、Main 丢失清理和 stop/start 重置，随后继续 Classic 媒体/重连回归。双语文档检查通过 31 对及一个明确例外。这些属于合成 Host 传输结果，L0 PASS、完整范围 L1 PARTIAL、L2–L5 NOT_RUN 不变，本里程碑没有新增刷写或真实手机交互。

## 备用视频传输 CI 与产物核验

SDK 344ce0c2a03dd545961bb11ccaa2d2805efa6954 通过 [sanitizer Host 及双语文档 CI](https://github.com/boa-z/rt-carplay/actions/runs/43771017888)，集成 b15c1c9c2acae83003f618f7c5eb573556518d48 通过 [Host、GUI 和 Headless CI](https://github.com/boa-z/luban-lite-carplay/actions/runs/43771086125)。三份产物全部下载，两份固件清单与准确源码版本、干净源码清单匹配；每份固件的 6 项 SHA256 和完整镜像 9 个组件的边界/CRC32 检查均通过。两组下载的 ELF/config 在本地通过有线配置及模拟模型检查。下载的 Host 日志确认两种加密备用视频场景均执行成功。

GUI 镜像 SHA256：cef23d88e9f745603f342320517df6b8ea0f23dde899c5b7d3c36f1cfd709ddc。Headless 镜像 SHA256：e9b8b1430161c72a8460742b7948230b2f891f6559d22f5697ed852040a0c8f1。清单继续标记 SIMULATED_ENGINE_RPM、PHONE_OFFER_REQUIRED 和 UI Sync 禁用，应用未启用实验性备用视频。两份镜像均未刷写，滚动 Preview Release 已跳过。此纯文档补充不改变镜像身份，L0 PASS、完整范围 L1 PARTIAL、L2–L5 NOT_RUN 不变。

## 可选 Vehicle State 服务级 Control

SDK d0a15bbd4312df82dca208ef3cb2b519cc91c357 为已有 Vehicle State 处理器增加[具类型的服务级 Control](../../../../packages/third-party/rt-carplay/docs/zh-CN/vehicle-controls.md)。宿主显式提供命令 IID/type、已有服务、请求/响应模式及纯准备/编码后提交回调。模型复制目录和参数，畸形批次在准备前拒绝，未知 IID 沿用参考值 -6735，仅在完整响应编码成功后调用执行回调，响应保留/重试复用现有 Receiver 交付路径。不推测真实车辆映射、执行器、图像包或命令，D213 模拟发动机配置仍不注册 Control。

本地完整 SDK Make、启用 RTCP_SANITIZE 的 CMake 72/72 项、完整集成 Host 套件及双语文档检查（32 对、一个明确例外）通过。独立 Python OPACK/plistlib 检查验证输入、响应、服务元数据及重排/不匹配目录拒绝。加密公共 Receiver 场景在 Normal/HIGH 执行 Control，拒绝含后续畸形命令的批次且不提交前一个命令，保持 HIGH 读取、新 seed 重开及 Classic 媒体/重连回归。合成命令执行不能证明真实 iPhone 或执行器动作，L0 PASS、完整范围 L1 PARTIAL、L2–L5 NOT_RUN 不变。

新的只读设备检查发现 COM9 仍注册，一次 aiburn 命令返回缓冲运行文本，但没有确认的 shell 提示符或有效显示状态；后续正确的 carplay_display status 查询没有运行输出，升级接口未发现 USB 升级设备。这些观察不能确认板卡健康状态或 shell 不可用原因。原始输出存放于 Git 外的私有设备目录，没有进入升级模式、刷写、复位、修改配对文件或进行手机交互。实机验收仍需先确认调试通道，并在刷写前重新备份和核对配对文件。

[CI 与 D213 刷写检查点](evidence/d213-vehicle-controls-flash-20261115.md)记录精确固件哈希、SDK 纯测试后续提交、新配对备份及恢复校验，以及尚待完成的实机验收。
