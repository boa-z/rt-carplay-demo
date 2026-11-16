[English](d213-ultra-feasibility.md) | [简体中文](d213-ultra-feasibility.zh-CN.md)

# D213 完整官方 Ultra 的编解码与图形可行性

当前范围更新（2026-11-15）：本开发环境不再要求完整官方 Ultra。本报告保留面向未来官方产品的技术限制分析；缺失的编解码、图形、资产和认证能力不作为当前可移植协议基础及 D213 融合 Demo 的验收阻塞项。参见[当前范围与验收记录](ultra-development.zh-CN.md#当前范围与验收)。

源码审查日期：2026-11-15。当前 D213 开发栈可以承载便携协议实验及独立的 Classic 视频与本地仪表融合演示，但现有编解码与图形接口不能证明完整官方 Ultra 渲染链路已经成立。继续增加协商消息本身不能填补这些缺口。

## 公开架构要求

Apple 的 [WWDC 2024 架构演讲](https://developer.apple.com/videos/play/wwdc2024/10111/)将 HEVC 视频流描述为必要能力，说明无线专用体验、使用图像与行为脚本的 OpenGL Local UI 渲染器，以及将 Remote UI、Local UI、Punch-through UI 合成为一帧后交给系统合成器的 OpenGL 合成器。审查当日重新获取了公开演讲文字。这是公开架构证据，不能替代完整合作伙伴规范或接入/认证结论。

应用现有的硬件图层融合仍然有实际价值：同一块物理屏幕同时呈现已解码 CarPlay 视频与独立 LVGL 仪表。该实现不执行官方资产/脚本包，也没有复现演讲所述的渲染与呈现同步契约。

## 当前源码证据

| 领域 | 源码依据 | 影响 |
| --- | --- | --- |
| 编解码标识 | [mpp_dec_type.h](../../../../packages/artinchip/mpp/include/mpp_dec_type.h)定义 H264、MJPEG、PNG、AICP 解码标识。 | 此 API 没有 HEVC 解码标识；仅补一个枚举也不能实现解码器。 |
| 解码分发 | [mpp_decoder.c](../../../../packages/artinchip/mpp/ve/decoder/mpp_decoder.c)分发 JPEG、按条件启用的 H.264、PNG、按条件启用的 AICP，不支持的类型返回 NULL。 | 实际工厂没有 HEVC 实现或分支。 |
| 构建选择 | [MPP Kconfig](../../../../packages/artinchip/mpp/Kconfig)通过 VE 版本依赖选择 H.264 解码器；[MPP SConscript](../../../../packages/artinchip/mpp/SConscript)收集 JPEG、PNG、zlib 及按条件启用的 H.264 源码。 | Ultra 演示开关不会为固件增加 HEVC 实现。 |
| 图形加速器 | [mpp_ge.h](../../../../packages/artinchip/mpp/include/mpp_ge.h)提供填充、bitblt、旋转、命令提交与同步。 | 这些操作支持应用图形，但不证明存在 OpenGL 上下文、着色器管线或官方 Local UI 运行时。 |
| 单屏应用 | [video_layer_policy.c](../ui/video_layer_policy.c)与 [fusion_display.c](../app/fusion_display.c)管理 DE/LVGL 分区、视频位置、模式切换及输入归属。 | 已测应用契约仍需与官方渲染器、资产和呈现验收区分。 |

对当前 ArtInChip MPP/BSP、构建配置及 D21x 目标的限定源码检索，没有找到 HEVC 解码器或板级 OpenGL/EGL 集成入口。内置 LVGL 的 ThorVG 头文件存在通用 OpenGL 引擎声明和未支持引擎的注释；这些声明不能证明 D213 有可用图形驱动。该结论只针对当前检出源码与集成路径，不能推断所有厂商 SDK 或芯片硅片能力。

## 仍需取得的证据

| 问题 | 当前结论 | 所需证据 |
| --- | --- | --- |
| 当前 MPP 工厂能否解码 HEVC？ | 已审查的工厂/构建路径没有实现。 | 声明 HEVC 前，需真实解码后端、支持的 profile/level/色度/位深范围及错误、生命周期处理。 |
| D213 硅片是否能通过其他受支持厂商栈加速 HEVC 或提供适用 OpenGL？ | UNKNOWN。 | 芯片/厂商权威资料、受支持驱动与运行时，以及后续实机测量。 |
| 软件编解码器/渲染器能否达到目标？ | NOT_RUN。 | 维护中的便携实现、许可证核查、内存预算，以及无线、解码、渲染、音频并行时的 D213 实测，不能从 Host 测试通过推断 CPU 可行性。 |
| DE/LVGL 融合是否满足官方 Local UI？ | 尚未证明。 | 按适用集成契约完成获授权包的校验/执行、渲染行为、图层语义、时间戳与呈现同步。 |
| 是否具备完整官方产品接入/认证条件？ | 尚未确认。 | 适用的官方接入、集成要求与认证证据；开源协议实验不能替代该流程。 |

未完成的手机侧能力声明继续关闭。可以按各自验收等级推进平台无关 Vehicle State/UI Sync 基础和真实 iPhone 协议实验。在将 D213 定为最终完整官方 Ultra 产品平台前，需解决上述编解码与渲染问题。第二路逻辑视频流或相似仪表外观不能回答这些问题。

## 验收边界

本研究仅补充 L0 源码依据，L1 对应另行记录的 Host 协议测试。[首次融合刷写记录](evidence/d213-fusion-first-flash-20261115.md)标明开发板镜像与身份文件恢复情况，完整 L2 物理合成/输入验收仍为 NOT_RUN。L3 真实 iPhone Ultra 协商、L4 真实 iPhone Vehicle State/UI Sync、L5 独立 Type 111 仍为 NOT_RUN。本研究没有新增刷写、编解码基准、图形驱动测试或手机交互。
