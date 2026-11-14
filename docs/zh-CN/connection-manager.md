[English](../connection-manager.md) | [简体中文](connection-manager.md)

# 应用连接管理器

## 职责

`app/connection_manager.c` 负责应用连接 Worker、Receiver 启停、USB 配对、无线 iAP2 Handover、连接阶段发布、配对确认请求、配对文件操作和音频测试调度。该模块不包含 LVGL 头文件，也不依赖 LVGL 符号。`ui/lvgl_launcher.c` 负责 LVGL 对象、显示交接策略和启动页指针队列。UI 通过 `app/connection_manager.h` 发送请求并读取管理器状态副本。协议端点拆除仍由 Receiver Runtime 负责。

此次抽取完成 V2.1 重构所需的一项边界整理，保留已有 USB/无线模式选择和连接时序。有线、无线同时待命、自动仲裁以及 Launcher V2 视觉改版属于后续工作，此次改动不宣称已实现这些功能。

## 启动与生命周期

应用启动 Owner 调用 `carplay_connection_manager_start()`。初始化操作本身不支持并发。成功调用具有幂等性。互斥锁分配、Worker 创建或启动失败返回 `RTCP_ENOMEM` 或 `RTCP_EIO`；Worker 创建或启动失败后可重试，不会生成第二个 Worker。管理器与状态互斥锁具有应用生命周期，此版本不提供管理器销毁 API。

现有 GUI 配置在创建启动页后启动管理器。手动 Headless 配置保留原有 CLI Receiver 生命周期。管理器源码可在没有 LVGL 的条件下独立编译，支持启用或禁用无线、音频测试；这种编译验证不等于已在 Headless 固件中启用自动连接策略。

## 请求与快照

`carplay_connection_status()` 在管理器互斥锁保护下复制当前连接阶段、错误、配对挑战、端点和诊断计数。管理器尚未建立或非阻塞加锁失败时返回 false。调用方拥有结果副本，后续 Worker 更新不会改变该副本。传输、认证和媒体就绪仍保留 SDK 中的独立状态，应用阶段只是用于展示的汇总。

模式选择、重试、蓝牙确认、配对文件扫描或删除及音频测试控制均通过管理器函数进入。Worker 保持对会话拆除、文件 I/O 和音频设备测试启停的所有权。文件与音频页面会在 Worker 处理请求前预留连接所有权。无线待命时打开文件管理器，会先停止使用借用状态的 iAP2 和 Receiver，再访问持久化记录。违反当前阶段或资源预留的请求会被忽略，保持现有 UI 策略。

文件列表快照 API 将记录复制到调用方提供的存储中，仅在出现新列表时更新版本号。UI 按钮的用户数据指向 UI 自己持有的副本。音频快照同样复制选定设置和状态。音频测试增益调整通过管理器请求，由 Worker 应用；不改变音频协商、解码、混音、麦克风加密或 AEC 行为。

`carplay_connection_touch()` 根据管理器的关闭阶段限制输入，再调用 Receiver 的线程安全队列。Receiver 还使触摸、Siri 入队与 Start/Stop 互斥，防止访问已销毁的队列锁。启动页自身的指针锁与管理器状态锁相互独立。

## 验证

从 `packages/third-party/rt-carplay` 执行，`SDK_ROOT` 指向本仓库：

```sh
make check-lubanlite-demo SDK_ROOT=/path/to/luban-lite-carplay
make -j8 check
```

应用回归使用 SDK LVGL 头文件编译实际管理器和启动页源码，并在 ASan/UBSan 下以模拟 USB、无线、媒体和调度服务执行实际 Worker。覆盖信任或存储失败、重插、网络与首帧等待、静默关闭、异步蓝牙失败、无线模式切换、配对管理互斥、指针路由与溢出以及音频测试所有权。新增请求 API 检查覆盖启动失败后重试、幂等启动、状态副本、文件列表版本和管理页面互斥。另有两项语法检查在不提供 LVGL 头文件路径的条件下编译实际管理器，分别启用、禁用音频及无线功能。

SDK Make 通过 58 个测试程序及旧配置加密端到端变体，CTest 通过 59 项，包含实际 RT-Thread 包装层生命周期回归。Host 测试和语法检查不能验证真实 USB 取消、蓝牙重连、音频设备释放或面板交接。GUI/Headless 交叉构建结果需要关联准确的已发布源码版本。在刷入匹配镜像并完成测试之前，这些改动的 D213 验证状态为 `NOT_RUN`。

## Receiver 集成

固定版本 SDK 现已接收公共 `rtcp_receiver_config_t.platform`、`.media` 和 `.iap2` 描述。RT-Thread 组合实现直接填写这些配置，其 Owner 通过公共 Receiver 函数发送 HID、Siri 和隧道 iAP2。视频仍为 Annex-B H.264，音频保留 LPCM/AAC/Opus 格式、流槽位及时间戳元数据。解码和设备 Worker、触摸焦点及 UI 交接仍由宿主负责。此 API 迁移不表示其余板级适配迁移或单例诊断所有权审查已经完成。参见 SDK 的 [Receiver API](../../../../../packages/third-party/rt-carplay/docs/zh-CN/receiver-api.md) 与[生命周期契约](../../../../../packages/third-party/rt-carplay/docs/zh-CN/lifecycle-and-ownership.md)。
