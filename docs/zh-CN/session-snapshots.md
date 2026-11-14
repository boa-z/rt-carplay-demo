[English](../session-snapshots.md) | [简体中文](session-snapshots.md)

# 会话快照与端点所有权

## 复制的元数据

`carplay_app_iap2_info()` 与 `carplay_app_wired_info()` 将会话字符串复制到调用方结构中的数组。复制的名称、SSID、口令、设备标识和公钥在 Receiver 停止、重新启动后仍有效。只有 `source_version` 指向不可变的 SDK 常量。调用方须使用更新后的宿主头文件重新编译，通过复制填写数组成员，不再直接赋字符串指针。这是 Luban-Lite 应用 API 的变化，不改变可移植 Receiver API。

`carplay_app_display_info()` 复制显示尺寸与配对 PIN。启动页与启动命令使用该副本。兼容的显示尺寸查询在生命周期变更期间返回零。旧的指针式 PIN 查询仍限定在串行化的生命周期 Owner 中使用；跨线程调用方必须改用复制的显示信息结构。

## 查询与生命周期顺序

读取者进入会话可重置存储之外的共享原子计数，可以与其他读取者以及触摸、Siri 请求并发。Start/Stop 串行化生命周期入口，禁止新读取者进入，并以休眠方式等待已有读取者返回，然后才重置状态或销毁会话互斥锁。此期间到达的查询返回 `RTCP_ESTATE`。显示查询失败时清空输出。所有操作均面向普通线程，不用于中断处理。

调试计数查询在访问会话互斥锁期间持有读取引用；HID 计数及加密事件就绪状态使用原子变量。Shell 状态命令共用已有生命周期保护，并在队列锁下复制队列计数。配对持久化通过独立于会话重置的原子变量发布实际写入结果，不会将查询竞争报告成配对存储失败。

## CarPlay Control 命令

轮询 Worker 拥有 mDNS Browser、端点选择和 Control 连接计数器。`carplay_ctrl on|off|connect` 只在会话互斥锁和生命周期门控保护下排队请求，Worker 在下一次 Control tick 之前应用请求。`connect` 保持既有重试间隔计算，不会隐式启用已禁用的 Browser。多个待处理的开关请求合并为最新期望值；重复连接请求合并为一次重试请求。

`carplay_ctrl status` 打印由所有者发布的复制快照，包含端点字符串，并分别报告待处理请求与已应用状态。Shell 不调用 Browser API。互斥锁忙时发布可能延迟一个轮询周期；网络 I/O 不持有该互斥锁。Receiver 未运行或正在退出，或没有可用 Browser 时执行 Control 控制命令，返回 `RTCP_ESTATE`；互斥锁争用返回 `RTCP_ETIMEDOUT`；无效参数返回 `RTCP_EINVAL`。运行中的有线 Receiver 可以通过 `status` 报告 Control 不可用。

## iAP2 回调缓冲区

无线与 USB iAP2 链路各自保存端点结构。Wi-Fi、AirPlay Provider 回调返回指向链路持久存储的指针，不返回临时栈快照中的指针。查询成功时更新链路副本；查询失败时保留上次有效副本，包括会话身份配置仍持有的指针。链路 Owner 在重置链路存储前仍须结束对应 iAP2 会话。

本次只改变所有权，不改变能力声明、Pair-Setup、Pair-Verify、iAP2 消息时序、端点值、音频协商和媒体行为。

## Worker 启动与静止状态

蓝牙 Echo 在调度前发布原子运行状态，串行处理命令，并检查启动错误。创建或启动失败时移除回调并释放蓝牙占用。首次调度前收到停止请求时，跳过控制器启用和监听器注册。停止操作不能等待自身 Worker；有界等待超时后，资源仍归 Worker 所有，直到它完成断开与回调清理，并以最后一步发布静止状态。停止未运行的 Echo 不会断开其他蓝牙所有者。

有线 USB iAP2 Worker 同样检查线程启动结果，在失败时回滚 Session 和成功安装的 MFi Provider。Provider 安装失败时不会释放其他 Owner 的引用。run/running 标志和线程句柄均为原子变量，重置时保留原子存储，以允许并发观察者读取。拔线请求代次避免后续启动覆盖更早到达的拔线请求。首次调度前收到停止请求时跳过 Link-Up 发送，最终停止日志和 Provider 清理先于静止标志发布。停止拒绝等待自身，在三秒超时后保留流所有权；Shell `stop` 也必须先结束流 Owner，再关闭 USB Hook。

USB 应用操作与全部 Shell 命令共享一个非阻塞原子调用门。争用返回 `RTCP_ESTATE`，不会进入 Lockdown 或 Transport 代码。这保证单项操作串行化，也覆盖使用可复用描述符存储的状态命令；由多个操作组成的 pair/start 序列仍需一个应用 Owner 负责组织。拔线回调保持非阻塞，仅发布停止标志／请求代次。信任缓存结果通过原子变量发布。公开 Session 关闭接口在超时或拒绝等待自身时返回 `RTCP_ETIMEDOUT` 并保留流，Worker 结束后再次关闭即可完成清理。

无线 iAP2 服务通过原子生命周期 Gate 串行化 start/stop，争用和等待自身返回 `RTCP_ESTATE`。run 标志、Worker 句柄、运行状态及跨 Owner 的进度／计数器均使用原子类型。Bootstrap 和 AirPlay Tunnel 重置时逐字段存储诊断值，不以 `memset` 清零正在被读取的原子存储。状态读取不解引用 Session，只复制观察字段；这些字段可能反映相邻 Owner 步骤，并非事务性的一次 Session 状态转换。

无线异步停止只发布请求代次和停止标志。请求代次避免蓝牙占用或状态重置期间到达的停止请求被后续启动覆盖。RFCOMM Owner 在现有有界 500 ms accept／100 ms receive 等待或正在执行的平台启动调用结束后观察停止，并自行断开 Transport。首次调度前收到停止请求时完全跳过 MFi 探测和蓝牙启用。在 MFi 探测、蓝牙启动、Session 创建或 accept 成功期间停止，均不能继续进入后续协议链路建立。同步停止最多等待三秒，超时后保留 Worker 资源所有权，并拒绝重启直到最终清理。只释放成功安装的 MFi Provider，避免安装失败时减少其他服务的 Provider 引用。Session 销毁、Provider 释放、蓝牙占用释放及最终日志全部先于静止状态发布。

## 触摸读取者退出

触摸 start/stop 共享非阻塞调用门；争用或等待自身返回 `RTCP_ESTATE`。运行标志、触点数量和诊断计数均为原子变量，通过存储重置，不并发执行 `memset`。停止最多等待读取者一秒；`RTCP_ETIMEDOUT` 保留设备、触点缓冲、唤醒信号量和借用的回调／上下文。即使读取者随后退出，再次停止并关闭资源前仍拒绝重启。Receiver 关闭 Owner 在结束回调状态前重试停止。因此，阻塞的设备读取可延迟关闭；超时不意味着可以释放该上下文。

实际源码触摸回归检查原有所有权、IRQ 和坐标行为，并在 ASan/UBSan 下新增调用门争用、等待自身、超时保留、读取者延迟退出后再次清理，以及 32 轮立即停止／重启。设备／I2C 调度仍由模型提供；物理 IRQ 交付和触摸响应需要开发板测试。

## 已执行回归与剩余审查

`make -C application/rt-thread/rtcarplay -j8 check` 使用真实 Linux 协议、熵源和模拟的 RT-Thread 调度、设备接口执行实际组合源码，覆盖有线、无线、显示元数据复制，IPv6 就绪状态，源数据清空后的快照保留，并发读取，写入方等待真实 pthread 读取者退出，拆除期间拒绝查询，以及已有的 32 轮生命周期测试。实际 iAP2 Provider 回归验证返回指针归属、源数据结束以及刷新失败时已保留字符串仍有效。组合回归还使用真实 Browser 解析实际 DNS SRV 响应，检查请求排队与应用状态、Browser 销毁后端点副本有效，以及销毁期间拒绝命令。两者均在 Host CI Job 中通过 ASan/UBSan 执行。

`check-wireless-iap2-worker` 编译实际无线状态、回调、Owner、Tunnel 和 start/stop 函数，链接真实 SDK iAP2 Session，仅模拟蓝牙／调度／MFi 探测边界。覆盖创建／启动失败、生命周期争用、占用期间和调度前停止、probe/enable/Session 创建／accept 期间停止、Provider 安装失败时的所有权、等待自身、有界超时保留，以及带并发状态读取者的 32 轮重启／Tunnel 重置。ASan/UBSan 检查内存及未定义行为；这不是 ThreadSanitizer 结果，也不是线上 MFi 握手测试。

`check-usb-iap2-worker` 提取实际链路类型、重置／清理、Worker、调用门及公开／Shell 包装函数，链接真实 SDK iAP2 Session，仅模拟线程调度、Provider 释放和阻塞命令 I/O。ASan/UBSan 覆盖创建／启动回滚、安装失败后的清理所有权、初始化／线程创建期间拔线、首次调度前停止、等待自身、超时保留流、真实竞争 pthread 被全部包装函数拒绝、错误路径释放调用门，以及 32 轮重启。不覆盖真实 Lockdown 信任过程或物理 USB 调度。

这些测试不代表实机配对、重连或播放通过。其他设备 Worker 生命周期仍需审查。完整 V2.1 验收与后续 Launcher V2 工作仍未完成。
