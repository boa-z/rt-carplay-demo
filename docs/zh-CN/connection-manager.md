[English](../connection-manager.md) | [简体中文](connection-manager.md)

# 应用自动连接管理器

## 职责

`app/connection_manager.c` 拥有应用连接 Worker、Receiver 启停、USB 配对、无线 iAP2 交接、阶段发布、配对确认、设备信任记录和音频测试调度，独立于 LVGL。`ui/lvgl_launcher.c` 拥有展示和指针队列，发送请求并读取调用方自有快照。协议端点拆除归独立 SDK Receiver 所有。

## 启动与生命周期

`RTCARPLAY_AUTO_CONNECT` 在有无 LVGL 时启用相同应用 Owner。Launcher V2 选中该配置，在 UI/输入注册后启动；Headless 从 `main()` 启动。无线选项齐全时，开机启用 USB 发现并启动 SoftAP/mDNS/Bluetooth/iAP2，无需选择模式。USB 配置选择 Hook 在 CherryUSB 启动前的静态初始化阶段启用，因此开机前已插入的手机可在 GUI 启动前枚举，稍后 Owner 读取保留快照。手动构建继续使用显式 USB 启用行为。

`carplay_connection_manager_start()` 由单个启动 Owner 调用，启动成功后幂等；分配、调度失败返回 `RTCP_ENOMEM` 或 `RTCP_EIO`，GUI 启动 Owner 每秒重试失败的 Worker 启动，Headless main 使用相同间隔；成功后幂等，不创建第二个 Worker。Worker 和互斥锁具有应用生命周期，没有管理器销毁 API。USB 服务 Open 失败每五秒重试，无线继续可用。

## 仲裁与恢复

只有一个 Receiver 和一个应用会话 Owner 运行。无线空闲发现期间，如果尚未观察到 Bluetooth、Handoff 或 AirPlay 竞争者，已连接的 USB 手机取得连接权。一旦无线配对或 Handoff 开始，USB 等待该次尝试结束，不抢占活动会话。USB 尝试先收回无线 iAP2 Owner、Provider 和 Receiver，再启动 IPv6 Receiver。无线运行时 USB 枚举持续启用。由于开发板共享 Receiver、媒体和 MFi 资源，有线尝试或会话期间暂停另一条无线会话；同时待命不表示两个协议会话同时运行。

无线保留既有 Bond 恢复、RFCOMM 服务、MFi 和 Wi-Fi Handoff 流程。原 Owner 两秒后主动寻呼已保存 Bond，每 30 秒重试，在 Handoff 或活动会话期间暂停寻呼。未重写 Bond Key、SDP 或协议定时器。管理器另设失败尝试冷却时间，依次为 5、10、20、40 秒，之后最多 60 秒；一路冷却时另一路仍可被选择。正常断开使用一秒静默期。USB 拔出清除其冷却时间，Retry 清除两路冷却。截止时间比较处理 32 位 Tick 回绕。

USB Trust、IPv6 就绪和首帧等待保留原传输限制。GUI 的 LIVE 要求 `carplay_video_sink_has_presented()`，即图层提交和 vsync 成功，独立于解码帧计数。显示失败可能增加解码计数，但不能将会话标为 LIVE。Headless 没有物理显示，使用协议会话状态副本。LIVE 后协议会话丢失会触发清理和重连，即使 USB/iAP2 仍然连接。状态副本可能落后一个 100 ms 发布周期，丢线判断先观察会话已建立，再将后续 False 解释为断开。

清理先等待 USB 和无线 Owner，再停止 Receiver。Receiver 关闭重试媒体退出，然后释放借用资源。无法退出的 Worker 继续持有资源并阻止替换，不会为加快重连而强制释放。Host 调度模型验证顺序，物理 USB 取消及无线、设备实际退出仍需开发板测试。

## 请求、设置与快照

`carplay_connection_status()` 使用非阻塞管理器锁复制阶段、错误、发现状态、配对挑战、端点、计数和公共 Receiver 独立就绪字段；返回 False 表示没有可用快照。副本不会随后续发布改变。Stage 是 UI 摘要，传输、认证、会话、视频、音频和输入就绪仍然分开。

仅空闲可用的 `carplay_connection_set_wireless_enabled()` 选择两路自动发现或仅 USB，USB 始终启用。设置保存在 RAM，重启恢复两路发现。旧 `carplay_connection_select_mode()` 保留为手动诊断覆盖，Launcher V2 不调用它。`carplay_connection_retry()` 接收空闲、失败状态请求。Bluetooth 回复保留当前请求 Token，平台拒绝过期确认。

设备和音频页面预留连接所有权。无线待命先收回 iAP2/Receiver 再访问记录；删除前也会停止有线 Receiver，随后重新加载持久化。文件快照将条目和版本复制至 UI 存储，行回调不借用 Worker 数组。设备身份文件排除在删除范围外。Bluetooth 重置影响全部 Bond，UI 明确确认该范围。音频测试先释放 Receiver，再打开 sound0/dmic0，成功停止后才恢复。增益、测试命令由 Worker 应用。协商、解码、Jitter、Mixer/Direct、加密麦克风上行和 AEC 策略保持不变。

触摸先由管理器关闭阶段限制，再通过 Receiver Owner 输入接口入队。Launcher 指针互斥锁独立。只读设置和诊断不操作 SDK 对象。自动构建阻止手动生命周期 Shell 命令，改用管理器命令：

```sh
carplay_connection status
carplay_connection retry
carplay_connection wireless off
carplay_connection wireless on
```

## 验证

在 Luban-Lite 仓库执行：

```sh
make -C application/rt-thread/rtcarplay -j8 check
make -C application/rt-thread/rtcarplay check-auto-connections
make -C application/rt-thread/rtcarplay check-lubanlite-demo
make -C application/rt-thread/rtcarplay check-lubanlite-usb
make -C application/rt-thread/rtcarplay check-lubanlite-video
```

实际自动 Worker 在 GUI 和 Headless 两种编译下各执行 32 轮开机发现、无线预留期间 USB 插入、USB 冷却重试、异步无线失败、USB Open 失败、首帧超时、USB 仍插入时 AirPlay 丢线、Receiver 启动失败和时钟回绕。独立自动构建在 Owner 订阅前执行实际 USB Class 绑定。视频测试在图层或 vsync 失败时拒绝就绪，即使解码计数非零。保留原手动模式、Trust/存储、管理器互斥、指针溢出与路由、音频所有权、生命周期和协议测试。另有两种不提供 LVGL 头文件的管理器语法编译。[Launcher 验证](mobility-cockpit-v4.md#证据)补充实际 LVGL 渲染和 UI 事件路由，连接值使用模型。

Host ASan/UBSan、LVGL 渲染与 GUI/Headless 交叉编译属于独立证据。本次准确 CI 版本和镜像记录在验证报告。D213 启动、显示、USB/无线重连、持久化、音频和触摸在匹配镜像实测前均为 `NOT_RUN`。

## Receiver 集成

宿主组合公共 `rtcp_receiver_config_t.platform`、`.media`、`.iap2` 描述，并将 HID、Siri 和隧道 iAP2 排队交给 Poll Owner。`carplay_app_session_state()` 复制 Owner 发布的公共状态，不借用协议对象。视频保持 Annex-B H.264，音频保持 LPCM/AAC/Opus 元数据和时间戳。SDK 保持独立，cherryusb-ipheth 保持独立驱动。参见 SDK [Receiver API](../../../../../packages/third-party/rt-carplay/docs/receiver-api.md)和[生命周期契约](../../../../../packages/third-party/rt-carplay/docs/lifecycle-and-ownership.md)。
