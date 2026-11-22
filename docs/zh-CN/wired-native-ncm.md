[English](../wired-native-ncm.md) | [简体中文](wired-native-ncm.md)

# 原生 CDC-NCM 有线 CarPlay

## 正式配置

D213 GUI defconfig 使用 `RTCARPLAY_NATIVE_NCM=y`、`RTCARPLAY_WIRED_NETIF="n0"`，关闭 `LPKG_USING_CHERRYUSB_IPHETH` 及上游单实例 NCM 类。同一手机只能由一个 USB 网络实现激活；Kconfig、SCons、编译保护及 CI 配置/ELF 检查共同约束。没有增加维护配置或 Headless 固件。正常构建关闭协议与网络头部追踪。

此前原生 NCM 候选仍绑定 Apple Ethernet/IPHETH 接口 2，并在配对后启用其 session link，同时通告原生 NCM 接口，因此没有真正隔离原生 NCM。已验收配置去除了竞争激活。对照支持独占 NCM 作为兼容性修复，但不能据此宣称 Apple 官方要求或手机内部策略。保留所选 NCM MAC 身份；尚未通过成功的独立对照证明该身份修正单独是否必要。

## 模块归属

| 归属 | 职责 |
| --- | --- |
| rt-carplay | 可移植 iAP2/AirPlay 协议、标识及会话消息，可选同步借用消息体的 iAP2 观察接口；不依赖 NCM、lwIP、GUI 或 SoC。 |
| Luban 应用/平台集成 | USB 配置选择、独占 NCM 绑定、描述符/NTB 编解码、RT-Thread 工作线程和 lwIP 网卡、DMA 所有权、网卡身份、连接策略与诊断。 |
| cherryusb-ipheth | 独立可选 Apple Ethernet 类、厂商帧格式及通用流量诊断；正式原生配置不链接它。 |
| CherryUSB 主机集成 | 通用多 qTD 短 IN 包完成修正，与 CarPlay 握手独立保留。 |

`native_ncm_codec.c` 虽是可移植传输代码，但不属于 CarPlay 协议，继续与受限应用适配器一起维护，不扩张 SDK 或另建仓库。未来其他使用方需要通用 NCM API 时，可连同测试提取。适配器使用公开 CherryUSB、RT-Thread/lwIP API，不修改或替换上游单实例类。本次不修改媒体、充电数值、StartSession 编码、无线策略或 OTA 行为。

## 传输与生命周期

通过 CDC Union、Ethernet 描述符解析第一个 Apple CDC-NCM 功能、数据备用设置与 MAC 字符串，不硬编码手机 PID、USB 配置编号或端点地址，第二个 NCM 功能不绑定。启动 iAP2 前校验通告接口与实际初始化接口一致。使用所选 NCM MAC 在 /info、配对和邀请创建前构建接收器身份，保留持久化 Ed25519 密钥，不重写运行中身份。

支持 NTB16/NCM0、最大 1514 字节以太网帧、最大 16 KiB 输入 NTB、8 KiB 输出存储、单个非链式 NDP 中最多 64 个数据报。校验或协商参数、对齐及可选过滤/CRC 控制。拒绝链式 NDP、带 CRC 帧及不兼容限制；这些是明确的兼容范围，不代表覆盖所有 iPhone 配置。

持久对齐 DMA 存储归一个异步 bulk 接收及可选通知接收所有。完成回调不分配载荷、不打印串口；线程校验 NTB 并复制以太网帧到 lwIP 自有 pbuf。断开时停止提交、排空发送、取消接收后才释放 hub port；取消失败则隔离所有权并要求重启，该异常仅做 Host 模拟，未实机强制触发。

本地链路就绪允许手机开启数据通道前完成 IPv6 DAD 与邀请，与实际 NETWORK_CONNECTION 通知分开记录，不宣告 WAN/互联网可用。专用链路关闭 DHCP 和路由自动配置。手机初期 NAK 导致少量有界 TX 超时，不等同于 CarPlay 失败，须结合 TCP、鉴权及媒体建立判断。

## 诊断与验证

`carplay_usb net` 显示所选网卡、原生接口、RX/TX/错误计数、观测载波及 IPv6 状态。可选 `RTCARPLAY_NCM_TRACE` 最多保留 24 条头部记录；`RTCARPLAY_WIRED_PROTOCOL_TRACE` 最多观察 16 条选定消息，原始字段限 128 字节并拆成短行。不记录媒体或认证密钥体。发送观测仅表示排队/提交，不代表手机接受。两项默认关闭，不是连接前提。

Linux 执行 `make -C application/rt-thread/rtcarplay check-native-ncm check-lubanlite-usb check-wired-identity check-wired-trace check-iap2-endpoints check-usb-iap2-worker`。测试覆盖实际编解码/线程函数、畸形元数据、双 NCM 功能、异步所有权与取消、原生/旧配置选择、排除 IPHETH 激活、接收器身份一致和密钥保留。ASan/UBSan 不替代 USB 总线或手机验收。CI 额外核查实际 GUI 配置与 ELF，防止后续静默重新激活竞争网络类。

2026-11-15 用户确认独占镜像 SHA256 `6ab86584e4315ce3ee2efec3e40aee78df822bd6d7f4c0538bb4f1a5c602ebda` 在 iPhone 17e、iPhone 13、iPhone 13 Pro Max（用户报告 iOS 27.2）上有线功能正常。17e 日志文件名结尾 `20261115_065722.txt`，对端报告 iOS 26.6.2，可见独占 NCM、所选网卡一致、RTSP 连接、/auth-setup 完成、首屏帧、视频可见及 PCM 音频包。日志含一次音频时间戳缺口，不能据此宣称无丢包或长期性能通过。原始日志含设备标识，仅保留本地。

该实测镜像开启了可选诊断；正式配置保持相同传输行为并关闭追踪。新构建发布镜像身份不同，不继承精确镜像的实机验收。最终发布镜像的重复插拔、长时间播放、通话/回声、无线 Siri 恢复、互联网共享及 OTA 回归仍需独立实测。本次启动修复不关闭既有无线 Siri 后冻结及有线帧节奏问题。
