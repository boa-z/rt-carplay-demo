[English](../wired-video-recovery.md) | [简体中文](wired-video-recovery.md)

# 有线视频接收进度优化

## 证据与边界

用户最新实机日志已完成 RTSP 配对、MFi 认证、屏幕建立、首帧 H.264、画面显示并到达 stage 5。其中有一次约 180 ms 的 PCM 时间戳缺口（44100 Hz 下缺少 7920 个采样），之后还有若干 330 采样缺口。日志未启用 USB/视频性能窗口，因此能确认连接及音频时间缺口，不能量化屏幕 FPS、解码负载、USB ACK 延迟，也不能认定卡顿的唯一根因。该片段没有异常不代表持续稳定性验收通过。附件包含认证材料，仅保存在已忽略的本地产物目录。

## 两项已复现的接收依赖

可移植 IPHETH 工作线程此前在每轮 RX 中都获取 tx_lock，即使端点未暂停。独立的同步 Bulk OUT 因此可能在一次 RX 完成后阻止唯一 RX URB 再次提交。新增真实工作线程回归用门闩阻塞 TX、完成 RX，要求 TX 尚未获准结束前已经再次提交 RX。旧代码失败，修复后 NCM 和 Legacy 两种模式均通过。正常 RX 现在绕过 TX 互斥锁；真实 TX STALL 仍获取该锁、重新检查暂停及停止状态、清除端点暂停并复位 DATA 切换位。暂停标志通过 OSAL 临界区发布。USB 超时、Carrier 检查、NCM 校验、断开所有权及取消失败隔离仍然有效。

RT-Thread lwIP 端口此前还使用 LWIP_TCPIP_CORE_LOCKING_INPUT=1。USB RX 适配层将每帧复制到自有 pbuf 后调用 netif 输入，直接模式在同一 RX 工作线程内持有核心锁执行协议输入。生成 TCP ACK 时可能同步等待 USB TX，拖延完成块解析和 RX 重新提交。两个维护中的 CarPlay defconfig 现在选择 RT_LWIP_QUEUED_INPUT，使用标准 lwIP 邮箱输入（LWIP_TCPIP_CORE_LOCKING_INPUT=0），同时保留 LWIP_TCPIP_CORE_LOCKING=1。TCP/IP 线程处理数据包及 ACK，RX 可独立重新提交。现有邮箱和输入消息池保持有界，满邮箱返回错误，适配层释放被拒绝的 pbuf 并计入 rx_input_fail。其他方案默认保持直接输入。不新增 DMA 缓冲、工作线程或自定义网络队列。

适配层首次注册 netif 时输出 input=queued 或 input=direct。此模式由平台策略选择，rt-carplay 保留现有可移植接口、协议和音视频解码路径。尚未证明上述任一依赖是实机唯一根因，不宣称具体 FPS 提升。

## 可执行验证

```sh
cmake -S packages/third-party/cherryusb-ipheth -B output/ipheth-host -DIPHETH_SANITIZE=ON -DCHERRYUSB_ROOT="$PWD/packages/third-party/cherryusb"
cmake --build output/ipheth-host -j8
ctest --test-dir output/ipheth-host --output-on-failure
make -C application/rt-thread/rtcarplay BUILD=build-network-validation -j8 check
```

驱动测试覆盖 NCM/Legacy 下门闩阻塞 TX 期间 RX 重新提交、TX 和 RX STALL 恢复、独立 Carrier 延迟、重连、分配失败、多设备及取消失败保留。应用测试执行真实 lwIP 输入和分发函数：直接模式按预期触发 RX 上下文断言，排队模式延后协议处理，满队列时 pbuf 仍归适配层，分发错误时释放，并验证后续输入恢复。同时使用 GUI/Headless defconfig 和 Legacy/AIC8800 默认值编译实际 lwipopts.h，保留 UDP 邮箱和字节界限回归。既有路由、音频、视频、自动会话、有线无线切换及生命周期检查仍属于完整应用检查。GUI/Headless 交叉构建和镜像身份另记于交付产物报告。

## 实机对比

使用同一手机、线缆、屏幕活动及音频负载对比上一 GUI 镜像和新镜像，每个镜像在测量负载前打开既有五秒统计：

```text
carplay_perf on
ipheth_status
carplay_connection status
carplay_usb net
```

至少采集 60 秒地图持续移动或列表滚动，先不播放音乐，再同时播放音乐。对比 rx/show FPS、sender_gap、assembly/process、owner/partial 间隔、queue/decode/present 耗时、IPHETH rx_parse/rx_rearm 和 TX 耗时，以及 rx_input_fail/rx_errors。sender 间隔大可能是手机内容未变化，静止首页不能用于测最大 FPS。稳定接收但显示慢与帧组装前存在间隔属于不同问题。出现邮箱/pbuf 失败或新增丢包时，不能宣称排队已解决问题。

重复 USB 重连和 USB/Wireless 切换、无线音视频及以太网共享回归。有线音乐至少测试 15 分钟，若发生异常，保留日志并用配套 ELF 解码。此前 EHCI 完成顺序修复继续保留。原 PC 与指令不一致、实机回声、音质及独立 inetshare 定时器池耗尽仍待分别验证，不能从接收路径解耦推断它们已修复。
