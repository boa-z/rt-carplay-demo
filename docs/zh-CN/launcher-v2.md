[English](../launcher-v2.md) | [简体中文](launcher-v2.md)

# Launcher V2

> 当前产品界面是 [Mobility Cockpit V4](mobility-cockpit-v4.md) 原生 UI，已在原位置换本页集合。本页记录 V2 范围，并继续作为其引入的连接管理器行为的参考；下文中的"设备"、"车机网络"、"音频"、"诊断"行描述的是已退役页面，UI 中不再存在。

## 应用范围

Launcher V2 基于 [V2.1 软件验证基线](refactoring-v21-verification.md)，在应用[自动连接管理器](connection-manager.md)之上实现 1024×600 LVGL 9 仪表盘。没有新建 SDK、向 SDK 加入 GUI 依赖、引入协议框架、复制驱动或创建开发分支。USB/无线策略和设备所有权保留在宿主应用。音频协商和处理保持冻结。

## 可用页面

| 页面 | 行为 |
| --- | --- |
| 首页 | USB/无线自动发现、连接步骤、实际发现状态、PIN/SSID、运行时间和重试 |
| 车机网络 | 首页 Ethernet 入口、网卡/网线状态、IPv4/掩码/网关/DNS、异步 Check now、HTTP 204 结果时间和独立互联网共享状态 |
| Bluetooth 配对 | 当前数字比较、同意请求或显示码，匹配请求的确认和取消 |
| 设备 | *(已退役，见 V4)* 已保存有线手机标识和 CarPlay 信任组、刷新、确认删除，以及明确重置全部 Bluetooth 手机 |
| 设置 | *(已退役，见 V4)* 仅空闲可修改无线发现，USB 始终启用；编译启用时控制真实麦克风静音；显示当前固件版本、提交哈希和 A/B 槽位，支持预览提交检查、绑定目标的确认、下载校验、非活动槽位写入及失败状态 |
| 音频 | *(已退役)* 保留 Tone/Silence/AAC、采样率/声道、增益/静音、启停、麦克风电平/回环和输入电平条，控制由 Owner 执行 |
| 诊断 | *(已退役)* 独立传输/认证/会话/媒体/输入状态、网络、无线/MFi、Trust/存储错误、解码计数、显示交接、恢复与重试等待 |

首页移除手动 USB/无线模式选择。深色卡片、一致间距、32 像素标题、高对比文字、少量强调色和独立诊断页面取代此前以密集调试信息为主的页面。面板使用阻挡背景触摸的暗色遮罩。设备名称由经过验证的存储标识生成，不编造友好 iPhone 名称。CarPlay 权限与 Bluetooth 重置属于组操作，明确确认范围。身份材料受到保护。设置即时生效，仅保存在 RAM，重启复位；不支持的麦克风控制被禁用。

视频通过宿主显示 Observer 在帧成功锁存后接管。视频活动期间 Launcher 停止使隐藏页面失效。断开收回 Sink 并恢复保存的 UI Alpha。如果已有解码视频但 UI 交接失败，可见启动页显示显示检查状态和诊断。这不能替代物理面板验证。Headless 使用相同连接 Owner，不编译 LVGL。

## Ethernet 检测

点击首页右上角 Ethernet 状态进入 Vehicle network，在 Ethernet 获取地址与网关后点击 Check now。现有单个后台网络 Worker 执行绑定 WAN 地址的已配置 HTTP 204 请求，LVGL 只读取复制的快照并提交请求。排队或检测期间合并重复请求，不增加连接 Worker 或修改协议。页面每 250 ms 刷新，网络观测每秒发布一次。

页面区分网卡存在、网线链路、取址、缺少网关、检测中、未检测、成功、失败和过期，显示配置的检测 URL 及结果时间。检测成功独立验证车机自身的 Ethernet 检测目标，不依赖 AP 是否可用，也不代表全部站点或 iPhone 路由已验证。DNS、超时与认证门户均可能导致失败。WAN 链路/地址/网关/掩码/DNS 变化清除旧证据，AP 变化只停止转发。保留每 15 秒自动检测与 45 秒证据有效期。未启用互联网共享的构建显示 Unavailable 并禁用 Check now。

## 复现 LVGL 预览

预览编译实际 Launcher 和仓库中的实际 LVGL 9 实现，在 1024×600 内存显示器中渲染并输出十二张 PPM 图像。测试检查可见页面边界，以及网络检测、不可用/未插网线/检测中/成功/失败状态、无线/麦克风设置、设备确认、音频 Start 和 Bluetooth 确认的真实 LVGL 事件。传输、媒体和状态输入明确使用模型，仅验证渲染与请求路由；图像不是开发板截图，也不是协议验收证据。

```sh
cmake -S application/rt-thread/rtcarplay/tests/launcher_preview -B build/launcher-preview -DCMAKE_BUILD_TYPE=Debug
cmake --build build/launcher-preview -j8
mkdir -p output/launcher-v2-preview
build/launcher-preview/launcher_preview output/launcher-v2-preview
```

固件工作流 Host Job 在完整 ASan/UBSan 应用测试之后执行这些命令，并独立上传预览。GUI 和 Headless 固件 Job 构建匹配 Bootloader 和完整 IMG。依赖与源码、镜像身份见 [CI](ci.md)。渲染不打开物理显示、USB 控制器、无线设备、扬声器或麦克风。

## 硬件验收清单

| 场景 | 状态 |
| --- | --- |
| 实机 Ethernet DHCP、无 AP/iPhone 的手动检测、拔线/恢复与认证门户 | NOT_RUN |
| 上电前插入手机，自动有线发现 | NOT_RUN |
| 开机无线发现、新配对和已保存 Bond 重连 | NOT_RUN |
| 无线配对/Handoff/活动期间插入 USB，不抢占会话 | NOT_RUN |
| 有线、无线拔线/断开/重连及重复循环 | NOT_RUN |
| 首个可见 CarPlay 帧、UI Alpha 交接和 Launcher 恢复 | NOT_RUN |
| 实际面板触摸、CarPlay 多点归属及弹窗命中 | NOT_RUN |
| Trust/Bond 删除、重启后持久化和存储失败 | NOT_RUN |
| sound0 播放、dmic0 电平/回环/静音和长时间稳定性 | NOT_RUN |

测试时记录准确源码版本、镜像 SHA256 和原始日志。此前 38217874 镜像的开机反馈与显示失败反馈不能验证 V2。音频路由、零 Stream 问题仍交接至 SDK 独立音频文档。交付不包含 PR。

[本地 Ethernet 验证与镜像身份](launcher-network-verification.md)。
