[English](../wired-startup.md) | [简体中文](wired-startup.md)

# 有线启动与配对性能

## 已观察到的边界

用户提供的实机日志显示 RSA 配对记录生成成功，耗时 6933 ms，随后 Pair、StartSession 和 CarKit StartService 均成功。日志停在 stage 3，即应用层等待网络的阶段。这能证明信任及 TLS 建立成功，不能证明 IPHETH 就绪、IPv6 有效、iAP2 识别、MFi 认证或视频正常。此前日志曾在同一网络阶段超时。本次片段尚不足以确定缺失的底层网络条件。

随后对 GUI 镜像 `bb2f254e6b33984cf71dc3135383c0e7f7c30c0611c3171d07918d887c6f76b6` 的实测推进到 stage 4：MFi 成功、识别接受、订阅及面向 `[FE80::DCB5:4FFF:FE73:DD03]:7000` 的 CarPlayStartSession 已发送。那次较早测试没有 AirPlay TCP/RTSP 或首帧证据。线程 suspend 和 TX 错误为零不能证明死锁或对端可达；缺少 RX/TX 总数及邀请后的快照。

2026-11-14，邀请恢复镜像交付后，用户确认有线 CarPlay 正常。所附日志显示首次邀请 ACK、五秒快照仍无 TCP、一次启动补发及 ACK，随后建立 RTSP、配对/验证和认证成功、event 连接、screen 连接、首个 H.264 帧、视频可见及 stage 5。本次运行已证实启动及画面恢复。日志时序支持有界补发有效，但不能确定手机忽略首次邀请的原因。日志包含新版恢复标记，没有嵌入式镜像哈希，因此刷入镜像身份依据本次测试会话关联，未通过固件输出独立核验。

同一日志记录配对记录生成耗时 3732 ms，44.1 kHz 双声道 PCM 报文进入 48 kHz 播放。同时出现 `inetshare` 线程中的 `MEMP_SYS_TIMEOUT` 池耗尽断言及音频时间戳缺口。这些仍为未解决问题；启动成功不代表互联网共享恢复、音质、通话回声、持续性能、触摸或重复重连已经验收。本次任务记录这些发现，不继续改变刚恢复正常的启动路径。

## 启动修改

CarKit 打开后，应用立即启动有线 iAP2 工作线程，同时等待网络就绪。读取 CarKit 识别及 MFi 数据不再以 IPHETH 地址有效为前提。保留原有 15 秒网络等待、单会话所有权及有界清理；iAP2 工作线程退出时提前结束等待。

可移植 SDK 收到有线 CarPlayAvailability 后，如果认证、端点可用性或发送容量使首次邀请无法发出，会保留待发送状态，在所有者线程的时间推进中重试。成功发送一次后清除，连接重置也会清除。这是在完成一次待发送邀请，不是周期性重复发送有线邀请。无线协商保留原有策略。仍然要求地址有效并通过 IPv6 重复地址检测，不伪造 carrier，也不把 tentative 地址直接标为有效。

iAP2 启动后及失败清理前会自动输出 USB 网络诊断。存在可选 RT-Thread IPHETH 诊断提供者时，日志包含接口标志、IPv6 第零槽位状态、RX 提交与完成数量、readiness、carrier 和传输错误。没有注册接口时也明确输出，从而区分注册缺失、接收未完成、carrier 未建立以及地址尚未有效。

## Stage 4 恢复与证据

此前 sent-once 标志会抑制后续所有有线 CarPlayAvailability 的响应。现在每次新的 availability 都会得到回答；端点暂时消失时保留待发送状态。已经接受 AirPlay TCP 或建立 event 连接后，停止继续发送有线邀请。无线邀请节奏保持原有策略。

应用在尚未接受 TCP 时，等待 `RTCARPLAY_WIRED_REINVITE_DELAY_MS`（默认 10000 ms）后额外发送一次有线启动邀请。设置为零关闭此兼容策略；可移植 SDK 默认关闭。等待时间是应用选择，不是 Apple 协议要求，也不会变成周期性邀请循环。DiPlay 在 `615b32ce0b40cd0b99ce94ba23a512001553684f` 的 [WiredStartAgain.kt](https://github.com/shihabal3amri/DiPlay/blob/615b32ce0b40cd0b99ce94ba23a512001553684f/shared/src/main/java/com/shilapi/xcertplay/orchestration/WiredStartAgain.kt) 记录了 USB 网络延迟就绪及会话复用问题，并采用有界二次邀请。其触发条件和 NCM 传输与本板不同，这一参考支持调查恢复策略，但不能证明本次 IPHETH 卡点的原因。不把私有协议行为当作官方规范。

stage 4 等待到 5、15、30 秒时自动记录接收器 TCP、认证、会话状态及 IPHETH 计数。IPv6 头部计数区分邻居请求与通告（`NS`/`NA`）、初始 TCP `SYN`、`SYNACK`、`RST`，并记录最后的 TCP 端口。RX 在 USB 收到、交给 lwIP 之前统计；TX 只统计 USB 提交成功，不证明 iPhone 接受。IPv6 扩展头只计入 IPv6 总数。不打印音视频或报文载荷。iAP2 日志区分 availability、首次发送、一次启动补发和链路 ACK；链路 ACK 不等于应用接受。

如果新镜像仍卡住，保持 USB 连接，保留自动快照，并间隔五秒执行两次以下只读命令：

```text
carplay_connection status
carplay_usb net
ipheth_status
netstat
```

收到目标端口为 7000 的 RX `SYN`、却没有 TX `SYNACK` 时，优先检查监听及协议栈交付；已发出 TX `SYNACK`、TCP 却未建立时，需要调查 USB 和对端。没有 RX `SYN` 时，先核对邀请 ACK、手机状态及邻居发现。这些是诊断分支，不是已经确认的根因。

## 配对优化

实际嵌入式 mbedTLS 源码为 2.28.1，与旧包选择标签不同。其 MPI 架构识别漏掉 RV64，导致 64 位 D213 目标使用 32 位大数单元。现已补齐 RV64 识别，使用 GCC 双字长运算。两把独立 RSA 密钥仍为 2048 位、指数 65537，素性检查、证书、TRNG 及配对缓存格式保持兼容。可选单调时钟单独记录配对记录生成时间，不计入手机信任弹窗等待；使用有效缓存时跳过生成。

## 可复现的软件检查

```sh
make -C packages/third-party/rt-carplay -o check-docs -j8 check
make -C application/rt-thread/rtcarplay BUILD=build-network-validation -j8 check
sh packages/third-party/mbedtls/tests/run-pair-mpi.sh output/pair-mpi-native
CC=riscv64-linux-gnu-gcc AR=riscv64-linux-gnu-ar RUNNER="qemu-riscv64 -L /usr/riscv64-linux-gnu" sh packages/third-party/mbedtls/tests/run-pair-mpi.sh output/pair-mpi-rv64
```

MPI 检查分别编译 32 位和自动识别的 64 位版本，运行上游算术向量，生成完整配对记录，验证证书链、私钥及指数，确认 host/root 身份独立，并验证熵源失败时拒绝生成。测试使用确定性熵，严禁用于固件。模拟器及 Host 耗时不代表 D213 实机耗时。应用回归模拟网络必须在 iAP2 启动后才能推进的手机；SDK 回归模拟只收到一次 availability、之后端点才就绪的情况，并确认随后只发送一次邀请。

## 实机验收

烧录产物报告中记录了校验值的镜像，保持 USB 连接，采集到识别、MFi、有线端点邀请及首帧显示的完整日志。如果启动仍失败，保留自动网络快照及清理前的初始 IPHETH 枚举信息。使用保存的配对记录再次连接，确认缓存复用，然后验证拔插重连、有线音视频与触摸及无线回退。Host 检查及镜像校验不能代替这些实机结果。尚未解决的有线音质、画面卡顿及通话声学回声仍需独立实机回归。

后续实机测试报告有线音乐播放死机（RISC-V 读访问异常：mcause=5、mepc=0x40069a22、mtval=0x0000006f216000d8、ra=0x4006b722），且有线界面仍然卡顿。因此，启动和出画验收不代表音乐播放或稳定性验收通过。已交付 GUI 符号表将 PC 定位在 ehci_urb_waitup 内，返回地址位于 USBH_IRQHandler，但已交付 IMG 在该 PC 处的字节编码为分支指令，而非读指令。用户随后确认实际烧录的就是该已交付 GUI IMG。镜像指令与异常报告之间的不一致仍未解释，尚不能归因到确切指令，也未完成修复的实机验收。音频栈遥测为总计 98304 字节、峰值使用 1876 字节，该样本不能证明音频栈耗尽。USB URB/QH 生命周期、缓存一致性和内存破坏均属于待排查方向，尚非确认根因。本次发布不宣称已经修复死机或卡顿。
