# 有线 USB 会话链路策略

[English](../wired-session-link.md)

## 证据

`1.txt` 和 `2.txt` 分别测试 `epoch-after-scan` 与 `iaa-before-scan`。两者均完成配对、CarKit TLS、MFi 和 identification，随后持续返回 `03 03 03 00`，RX 完成次数为零，端点 86 的 qTD 剩余字节不变，IPv6 保持 tentative。两者均在 stage 3 等待网络超时，回退无线后又出现 `MEMP_SYS_TIMEOUT` 耗尽。调整中断顺序没有恢复连接；目前没有隔离出具体坏提交，也不能证明丢失了 USB 完成通知。

当前沿用 Linux ipheth 的策略要求 vendor Carrier 正常才放行链路和发送。此前 host-ready 通知仅允许提前查询，状态字节为 3 时仍然阻塞。应用又等待有效 IPv6 才发出邀请，而链路 down 时 lwIP 无法发送 DAD。这是代码与日志确认的启动依赖，不代表解除依赖后这部 iPhone 一定连接成功。

社区参考 [DiPlay 615b32ce](https://github.com/shihabal3amri/DiPlay/tree/615b32ce0b40cd0b99ce94ba23a512001553684f) 直接操作 USB NCM 桥并使用主机配置的 IPv6 端点，没有 Linux Carrier 门槛。其 `IphoneUsbHost.kt` 把 `0x45` 用作 USB 模式诊断并提及 `3:3:3:0`；该请求的 index 为零，而本驱动查询使用 Ethernet 接口号。这不是 Apple 官方状态定义，不能据此把所有包含 3 的响应解释为 Carrier 正常。本实现未复制参考项目代码。

## 策略与归属

应用仅在 CarKit TLS 成功打开后，连同原有 host-ready 通知启用可选接口 `usbh_ipheth_session_link(hport, ready)`。板级适配器在临界区内检查 USB 挂载代次。正常关闭先停止 iAP2 owner，再撤销两种通知；停止失败则保留资源。物理断开清除策略，旧配对结果不能激活替换后的设备。SDK 没有增加依赖。

驱动要求确实成功提交 RX，且接收已挂起或已有观测就绪证据；之后由显式外部 USB 会话独立于 Carrier 授权有效链路与发送。其他主机默认保留原有 Carrier 策略。RX 提交失败不能打开链路或发送，查询错误或 down 不覆盖有效外部会话。原始 Carrier 与 RX 计数继续反映观测事实，`session_link` 和 `link_up` 分别表示策略及有效传输链路。有效链路不代表互联网可用或 AirPlay 接受连接。

lwIP 仍执行 DAD，应用仍等待地址有效才发出 `0x4301`。没有猜测端点、强设 preferred 地址、改变 alternate/NCM 选择或修改 SDK 协商和音视频。继续保留排队输入和 EHCI 安全回收。

静态定时器预算在 lwIP 自身内部公式之外增加 `RT_LWIP_SYS_TIMEOUT_EXTRA`，默认八项。仅计算协议栈内部定时器会遗漏每接口 mDNS 探测、NAPT 和网络监测。当前三个接口加 NAPT 和监测仍有余量；新增使用方应重新审查预算。这是单独复现的分配不足，不解释为最初零 RX 的原因。

## 验证与实机验收

驱动 ASan/UBSan 检查覆盖 NCM/Legacy 无首包且 Carrier 为 3、仅 hint 时拒绝发送、显式启用后实际发送、查询失败不伪造 Carrier、撤销、错误 hport、重连及 RX 提交失败。板级测试检查两种通知的挂载代次。真实静态 lwIP 分配器和调度器测试直接使用实际 port 预算，执行 200 轮重排，释放所有使用方并重新分配全部槽位；仅保留内部预算的反向对照重现池耗尽。该分配器测试不执行协议定时器业务。另用真实 lwIP IPv6 验证 link-down 阻止 DAD，启用链路后先发出 NS，地址才变为有效。应用 Host 检查与 GUI 构建属于软件证据。

使用 `output/wired-session-link-current/VERIFICATION.md` 中的 GUI 镜像与校验值。启动标记为 `policy=usb-session-v1`，诊断应显示 `session=enabled effective=up`，同时保留原始 Carrier。记录 DAD NS/TX、IPv6 有效、会话邀请、入站 TCP 和首帧。如果 TX 增长而 RX 仍为零，保留计数和 EHCI 快照，继续排查接收路径。重复冷启动和重插，再检查音乐、触摸及无线回退恢复。iPhone 实机验收尚待完成。

最终交付使用更新至主仓库 `ceaa76c9` 的独立文件副本，保留其中已提交的 GUI/OTA 配置、TF 挂载、OTA 清单和打包脚本，在副本中先重新编译 Bootloader，再构建 GUI，不向共享目录回写生成配置或 Bootloader。软件可以检查 OTA 包，实际安装和启动槽位切换仍需实机测试。用户已要求移除多余的 Headless 固件，本次只交付 GUI，不恢复该配置；Host 配置检查也只要求保留的 GUI 配置。

## 后续实机失败

随后提供的日志包含 `policy=usb-session-v1`、有效 IPv6、MFi/识别成功及已确认的 StartSession 邀请，但没有 AirPlay TCP 或画面。RX 始终为零，Ethernet OUT 反复返回 `-14`，实际字节数为零。CherryUSB 将 14 定义为 `USB_ERR_TIMEOUT`；`submit` 计数包含同步等待后返回的错误，不能据此认定传输在入队前被拒绝。原始 Carrier 仍为 down（`03 03 03 00`）。实际刷入镜像的哈希未知，因此日志只标识已执行策略，不据此认定具体镜像。生效 link 和 DAD 完成没有恢复真实通信，本次观察的实机验收为 FAIL。

随后直接通过串口捕获到手机已连接时的 stage-4 超时，RX/TX 为零，TX 超时 41 次，没有入站 TCP。通过本机 AiBurn 服务和 COM9 可以执行只读命令并捕获日志。原始日志包含设备身份，仅保留在已忽略的本地 output。下一次连接应在取消前捕获 OUT 描述符，之后的网络状态快照无法观察已经取消的传输。继续排查时保留排队输入、描述符安全回收、OTA 工作和仅 GUI 配置。
