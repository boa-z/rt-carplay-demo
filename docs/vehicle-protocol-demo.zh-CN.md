[English](vehicle-protocol-demo.md) | [简体中文](vehicle-protocol-demo.zh-CN.md)

# D213 Vehicle State 协议实验入口

RTCARPLAY_VEHICLE_PROTOCOL_DEMO 是依赖 RTCARPLAY_ULTRA_DEMO 的显式开发选项。专用 Ultra GUI 和 Headless defconfig 启用它，普通 Classic 配置保持关闭。它将模拟内燃机模型接入现有 Receiver 的认证 Vehicle State 通道，并显式选择 vehicleStateProtocol 协商，为后续真实 iPhone 协议实验提供应用入口。这不能证明官方 Ultra、真实车辆数据或官方 Local UI 渲染。

Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI)。元数据参考版本 64a05b68358db52ed887169143afd5944fd6f1f7 的 native/crates/livi-cp/src/vehicle_state/engines.rs 及 vehicle_state.rs 中的 RPM 单位常量，以 C 重新表达，遵守 GPL-3.0-or-later 及 NOTICE 署名要求。参见随附 rt-carplay NOTICE 和应用可见法律声明。本功能没有包含主题资产、密钥或私有设备资料。

## 已实现模型

Plugin 名称为 rt-carplay SIMULATED，本地分配 plugin ID 为 0。模型只有一个内燃机附件（IID 2560、type 0x09000005）、一个服务（IID 2561、type 0x1A00000F）以及下列三项特征。IID 标识当前本地目录，不代表所有车型通用的分配。

| IID | 特征 type | 含义 | 初始值 | 行为 |
| --- | --- | --- | --- | --- |
| 2562 | 0x30000024 | 发动机转速 | 0 RPM | 车辆侧拥有，仅本地模拟器可更新，拒绝手机写入。 |
| 2563 | 0x3000004E | 发动机最大转速 | 6000 RPM | 固定值，拒绝本地和手机写入。 |
| 2564 | 0x3000004F | 发动机红线标记 | 5000 RPM | 固定值，拒绝本地和手机写入。 |

三项均为 Uint32，单位 981（每分钟转数），声明范围 0–6000、步长 1。模拟器每 250 ms 采样，十秒内从 0 升到 5000 RPM 再回到 0。这是应用模拟数据源的周期，不是推测的协议时限、呈现时钟或车辆采样要求。模拟器不会控制车辆。

此线协议目录不包含车速、挡位和电池 SOC，它们仍是本地仪表中单独且明确标注的模拟值。参考源码 set_gauges 将 rpm 映射到 ENGINE_RPM、fuelPct 映射到 FUEL_LEVEL，没有 speedKph 映射。vehicle_state/tests.rs 的 gauges_reach_the_phone_in_both_units 测试将 speedKph 作为额外输入，其通知断言仅包含燃油、续航、里程及油耗，不能据此认定已实现车速特征。燃油百分比及电机功率也不等于电池 SOC。真实映射及完整显示/plugin/车辆关系仍需确认。

## 生命周期与诊断

应用先创建模型再构建 /info，将同一个模型交给 Receiver，仅在认证手机提出对应能力时启用 vehicleStateProtocol。每次接受新控制器前，Receiver 所有者使用当前值刷新并校验 /info；活动会话继续使用不可变能力快照。模拟更新及协议操作都在 Receiver 轮询所有者上执行。失去认证时订阅清除，数据源继续；应用停止时先销毁 Receiver，再释放其借用模型。完整应用重启会重新创建模拟器。

SDK 处理 config/read/register、订阅变化的 HIGH 通知，并对拒绝的手机写入返回逐 IID 错误，沿用现有传输背压和会话重置规则。只声明已实现的三项数据，UI Sync、displayPanels、HEVC、altScreen 和 Type 111 继续不声明。实验没有配套渲染器或资产包，手机可能不协商或不显示该模型。

carplay_status 输出所有者发布的快照：SIMULATED 发动机转速、数据源错误、手机提议、接受启用状态、normal/HIGH 通道状态、请求/拒绝数、订阅/通知数、传输排队/重试/放弃计数及最后交付错误。通道状态为 0 空闲、1 监听、2 已连接、3 已关闭、4 失败。快照沿用约 100 ms 的诊断更新周期；停止后可能保留最后样本，应同时查看开头 running/stopped 状态。通知进入队列不能证明手机已接收、已显示或车辆已执行。Shell 线程不会解引用活动模型或 Receiver。

## 验证与待完成实机项目

Linux/WSL 执行 make -C application/rt-thread/rtcarplay check-vehicle-protocol-demo。测试分别编译开启和关闭选项的实际应用组合。开启版本检查处理器接入、初始未提议/未启用通道、无 UI 注册、数值变化后的 /info 刷新、启动失败回滚及多次启停销毁。模型测试覆盖固定值、订阅响应、HIGH 通知、同采样周期抑制、拒绝手机写入、三角波端点、会话重置清订阅及重建。Python plistlib 独立解码生成的 /info，验证每项身份、类型、范围、单位、归属及初始值，以及没有 UI/面板/HEVC 声明。

以上都是 Host 验证。刷写前必须重新导出并验证当前身份/配对文件，刷写后恢复并读回比对哈希。然后经 COM9 确认 SIMULATED 诊断，保留脱敏的手机能力提议、SETUP 和通道证据，实测 config/read/register/HIGH 通知，同时回归 Classic 音视频与重连。手机未提出能力只是观察结果，不能作为 Ultra 协商成功。本里程碑刷写、真实 iPhone 协商/交互及呈现仍为 NOT_RUN；L0 PASS、完整范围 L1 PARTIAL、L2–L5 NOT_RUN 不变。
