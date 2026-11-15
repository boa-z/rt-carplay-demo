[English](../media-transport-repair.md) | [简体中文](media-transport-repair.md)

# 媒体与有线传输修复

## 证据与验收边界

用户实测报告上一份音频镜像仍有远端通话回声和音乐异常。合成回声测试及构建通过不代表实机验收。提供的音乐日志在 44100 Hz 下多次出现 16384 个采样的正向缺口，约 372 ms。这是成功解码包之间的缺口，不能证明 Wi-Fi 丢包、某种解析错误或 64 KiB TCP 窗口。对于这两项用户观察，上一镜像保留为功能失败基线。

历史有线测量证实 USBMUX 长时间持有端口锁以及 Carrier/RX 延迟，但不能证明所有视频卡顿都有同一个根因。保留 NCM 解析约束；格式校验错误与网络投递错误继续分别诊断。

## 实现修改

### 音频接收与恢复

可移植接收器先处理音频 UDP，再处理视频；每个 owner 轮次对每条音频流最多读取 32 个已就绪的数据报。每次追加读取前执行零超时就绪检查，因此不要求端口提供非阻塞 socket，空 socket 也不会阻塞 owner。批次上限为其他音频流和控制业务保留处理机会。认证和重放拒绝保持强制执行。

应用层媒体 lane 2、7 的压缩队列容量改为 32 包；语音保持 16 包，本地音频保持 4 包。这是应对接收突发的有限储备，不是无限延迟队列。较大的 RTP 时间戳正向缺口不再清除已经解码的 PCM；较小缺口保留原有的有限补静音策略。仍须在板上检查队列溢出、锁竞争和解码失败；这些修改没有证明此前的数据具体丢在何处。

应用播放与麦克风 worker 使用优先级 13、5 tick 时间片，先于优先级 14 的视频和优先级 15 的接收控制；网络服务仍有更高优先级。有限解码预取、DAC 背压和无采集数据时的等待保留让出 CPU 的机会；此调度策略仍须在板上测量 CPU 并回归视频。

### 全双工回声处理

应用根据参考与采集采样的强相关性估计附加声学/参考延迟。每十个采集帧检查一次，在 0 至 200 ms 范围内按 5 ms 步长搜索，连续三次观测一致才调整延迟。静音和低相关的近端语音不训练估计器。手动延迟会关闭自动估计；auto 重新开启。延迟变化由麦克风所属 worker 重建语音处理器。

可移植 SpeexDSP 语音处理器使用 200 ms 回声滤波器并显式配置残余回声抑制，近端活动时减弱抑制。缺少参考时向回声处理器提供零参考，继续推进时间线，而不是绕过它；这不能重建缺失的播放采样。参考对齐、削波、处理余量和实际双讲清晰度仍须声学验收。

```text
carplay_status
carplay_audio_path
carplay_mic
carplay_mic delay auto
carplay_mic delay 20
carplay_mic aec off
carplay_mic aec on
```

### USBMUX 与 IPHETH

USBMUX IN、OUT 使用独立且持久的端点互斥锁，EP0 保留端口锁。端点锁覆盖 toggle 读取、提交、完成/取消和 toggle 更新。断开先撤销设备发布，再排空两个端点 owner，随后 CherryUSB 才能释放端口；排队的调用者在端点锁内重新检查发布状态。保留 generation 检查和取消失败时的 DMA 隔离。

IPHETH Carrier 查询在独立控制 worker 中执行。私有控制存储与端点 halt 恢复、初始化配置串行化。Carrier 等待期间，RX worker 继续消费并重挂原有的一个 URB。连续三次 Carrier 事务失败才把原先已连接的接口置为断开；成功查询得到明确 down 时仍立即生效。两个 worker 都确认退出后才释放同步资源。任意 worker 创建失败都会回滚网络适配和全部已分配资源。

### 缓存中的 EHCI 描述符

维护中的 HCD 生成文件来自受审补丁，没有修改上游源文件。缓存中的前驱 QH，其水平链接和硬件更新的 overlay 位于同一缓存行。unlink 修改此行前，HCD 只关闭受影响的调度，等待硬件状态确认不活动，再 invalidate 两个描述符、更新链接、clean 前驱并恢复此前的 enable 位。另一调度保持原状。有限等待内无法静止时保留描述符和 DMA 所有权，返回失败，避免摘链/释放活动内存。异步回收仍受 IAA 保护。

该方案明确优先保证正确性，不使用未经证明的缓存描述符无锁更新。unlink 时会短暂暂停对应控制器的相关调度，实际时延与吞吐必须测量；实现没有宣称零调度开销。只在 clean 前补一次 invalidate 而不静止 DMA，仍会留下原有竞态。

## 自动化验证

```sh
sh packages/third-party/cherryusb-luban/tests/run.sh
sh packages/third-party/cherryusb-luban/tests/run_control_probe.sh
cmake -S packages/third-party/cherryusb-ipheth -B build-ipheth-repair -DIPHETH_SANITIZE=ON -DCHERRYUSB_ROOT="$PWD/packages/third-party/cherryusb"
cmake --build build-ipheth-repair -j8
ctest --test-dir build-ipheth-repair --output-on-failure
make -C packages/third-party/rt-carplay -j8 check
make -C application/rt-thread/rtcarplay BUILD=build-network-validation -j8 check
```

新增回归覆盖：40 包加密 UDP 突发跨越 32 包公平性边界；人为慢 Carrier 事务期间 RX 仍有进展；两个 worker 创建失败与全部五处 OSAL 分配失败；保留前驱独立推进后的 DMA overlay；调度静止等待超时保留资源；保留另一调度；合成延迟估计，以及无关近端语音和静音拒绝。Host 结果与镜像打包结果在交付验证目录中分别报告。

## 必须进行的实机验收

记录镜像 SHA256 并保留完整串口输出。音乐播放前，以及同一首已下载音乐播放至少 20 秒后，采集 carplay_status 和 carplay_audio_path。以增量比较 decoded、decode_err、decode_max_us、arrival_max_ms、overflow、queue_busy 和输出设备错误。分别测试无线、有线，再测试通话/Siri 中断与恢复音乐。不能把音频传输认证或队列失败归结为编解码音质问题。

通话远端使用耳机，车机扬声器固定在适中音量。测试仅远端讲话、仅近端讲话和双讲；比较 AEC 开启与关闭，并保留 carplay_mic 计数。确认 auto_delay、delay_updates、参考命中/缺失、采集缺口、削波及相对于 10 ms 帧的处理耗时。持续参考缺失或 CPU 超时属于失败，不能因为 AEC 标志开启就判定成功。

测试持续有线视频/触摸/音频、物理拔插与自动重连。重复已有 Carrier on/off/on 实验；慢 Carrier 不应再拖延 RX 重挂。检查 rearm/parse/TX 长尾及 RX 分类错误。同时运行 Ethernet 共享，把此前用户验证的上网行为作为回归要求。在替换镜像取得这些测量前，音乐音质、回声降低和实机有线卡顿改善均保留待验收状态。

## 后续修复：进入 SDK 前的 UDP 丢包与有线链路通知

替换镜像的实机日志仍显示 44.1 kHz AAC 帧之后出现 16384 个采样点的正向缺口，即缺少 16 个 1024 采样点帧，约 372 ms。仅凭解码后时间戳缺口无法定位丢失位置。检查发现 RTL 目标继承了 DEFAULT_UDP_RECVMBOX_SIZE=1 和 RECV_BUFSIZE_DEFAULT=8192。高优先级 lwIP 线程可能在 CarPlay owner 运行前投递一批数据，超过任一限制时 recv_udp 直接释放报文。扩大应用压缩队列无法恢复这些包。

新增 Host 回归执行真实 lwIP 2.1.2 recv_udp 回调，仅模拟 OS 邮箱。按每包 736 字节（700 字节 AAC 加 36 字节传输封装）发送 17 包突发，旧限制恰好丢弃 16 包。只增槽位会在 8192 字节限制下仅保留 11 包。新 64 槽位/65536 字节限制完整保留 17 包，也完整保留更大的 64 包突发；第 65 包仍被槽位边界拒绝。PCM 大小报文另行验证字节边界。这证明了足以产生该现象的软件缺陷，不代表已经捕获到手机实际发出了同样的合成突发。

两个维护中的 CarPlay defconfig 均设置 RT_LWIP_UDP_RECVMBOX_SIZE=64 和 RT_LWIP_RECV_BUFSIZE=65536。其他目标维持原默认值，包括 AIC8800 的 32 槽位。回归使用两个 defconfig 及两种旧默认配置编译真实端口 lwipopts.h。字节上限并非预分配的负载内存，仍受全局 pbuf 数量与有限邮箱约束。接收线程改为不等待 mDNS，并在会话 socket 上最多等待 10 ms，避免每次音频调度先受无关 mDNS 的 10 ms 等待拖延。

carplay_status 区分 delivered 负载、sink_rejected、malformed、replay 丢弃、sequence_missing 和 sequence_reordered。序号统计发生在负载认证成功后，但 RTP 序号字段本身不在认证 AAD 内；这些计数属于诊断观察，不能作为经过认证的丢包证据。正向缺口可能被随后乱序/重传填补，因此 sequence_missing 不是最终丢失总数。poll_gap_max_us 测量可读音频/控制调度之间的间隔，包含静音期间，不能当作 CPU 阻塞时间。AAC 拒绝日志最多输出前三次解码失败的 error、已消费字节数、采样数、声道与采样率。编解码校验、认证和重放保护继续生效。

有线日志从 stage=3 直接到 stage=6，未收到视频帧，说明等待有效有线 IPv6 端点期间退出，还没到 stage=4 启动 iAP2，不能据此认定视频解码失败。另一个可复现缺陷是共享 Ethernet RX 邮箱满时，旧 IPHETH 适配器丢失唯一 link-up 通知，carrier 持续 up 时也不再重试。启用实体网口 RX 线程后会使用这条路径。IPHETH 适配器现通过同步、受核心锁串行化的 netif API 完成 link up/down 和 detach，与 RT-Thread 无 RX 线程时的行为一致，并绕过共享邮箱。真实适配器回归确认模拟 Ethernet 邮箱满时仍能完成 link up/down，没有强制 carrier up 或跳过 IPv6 DAD。连接 owner 在清理前记录 stage、返回码、连接状态及模式变化；确认本次实机失败的具体原因仍需完整日志，以及故障时的 ipheth_status/ifconfig。

新增回归可通过 make -C application/rt-thread/rtcarplay check-udp-burst 执行，也已纳入常规应用 check 目标。无线播放已下载音乐至少 20 秒，前后记录 carplay_status 和 carplay_audio_path，再测试 Siri/通话中断及有线投屏。有线启动失败时保留新增 wired-exit 行、ipheth_status、ifconfig 和 carplay_connection status。替换镜像的实机音乐质量、回声表现、有线首帧/重连及 Ethernet 共享回归，在开发板/iPhone 实测前均为 NOT_RUN。

## 后续启动验收

后续启动实现在 stage 3 开始 iAP2，同时推进网络，不再等到 stage 4。用户确认邀请恢复镜像的有线 CarPlay 正常；所附日志显示一次额外邀请后视频可见，进入 stage 5。当前时序及准确证据边界见[有线启动](../wired-startup.md)。日志包含音频 PCM 接收，但仍有音频时间戳缺口及互联网共享定时器池断言。本次报告未独立验收实机回声、音质、重复重连、触摸和持续性能。

后续实机测试报告有线音乐播放死机（RISC-V 读访问异常：mcause=5、mepc=0x40069a22、mtval=0x0000006f216000d8、ra=0x4006b722），且有线界面仍然卡顿。因此，启动和出画验收不代表音乐播放或稳定性验收通过。已交付 GUI 符号表将 PC 定位在 ehci_urb_waitup 内，返回地址位于 USBH_IRQHandler，但已交付 IMG 在该 PC 处的字节编码为分支指令，而非读指令。必须先确认实际烧录镜像身份，才能归因到确切指令或宣称修复。音频栈遥测为总计 98304 字节、峰值使用 1876 字节，该样本不能证明音频栈耗尽。USB URB/QH 生命周期、缓存一致性和内存破坏均属于待排查方向，尚非确认根因。本次发布不宣称已经修复死机或卡顿。
