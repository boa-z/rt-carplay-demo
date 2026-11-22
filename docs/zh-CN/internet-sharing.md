[English](../internet-sharing.md) | [简体中文](internet-sharing.md)

# Ethernet 互联网共享

## 范围与证据

本可选 IPv4 网关使用 D213 Ethernet 作为 WAN、既有 CarPlay Wi-Fi AP 作为 LAN，实现位于宿主应用和实际使用的 RT-Thread lwIP 2.1.2 集成层。可移植 rt-carplay SDK、iAP2、Apple Vendor IE、会话仲裁及音视频算法保持不变。Ethernet 和共享直接加入既有 GUI 配置，Headless 继续作为关闭共享的独立兼容回归。Kconfig 开关可以独立于 Ethernet 关闭共享。不维护额外的 Internet defconfig，启用功能也不代表通过 iPhone 互操作认证。

| 证据类别 | 结论 |
| --- | --- |
| 公开标准 | IPv4 路由、NAPT、DHCP 路由器/DNS 选项和普通 UDP/TCP DNS 提供标准网关路径，见 [RFC 3022](https://www.rfc-editor.org/rfc/rfc3022) 和 [RFC 2132](https://www.rfc-editor.org/rfc/rfc2132)。 |
| Apple 公开信息 | [Developing CarPlay Systems, Part 1](https://developer.apple.com/videos/play/wwdc2016/722/) 与 [Part 2](https://developer.apple.com/videos/play/wwdc2016/723/)介绍 CarPlay 集成，[Apple 设置指引](https://support.apple.com/en-us/108415)介绍无线关联。这些来源不能确定此处可使用的公开互联网可用标志或消息。 |
| 社区实现 | DiPlay 提交 `615b32ce0b40cd0b99ce94ba23a512001553684f` 将其 [Apple Device IE 策略](https://github.com/shihabal3amri/DiPlay/blob/615b32ce0b40cd0b99ce94ba23a512001553684f/shared/src/main/java/com/shilapi/xcertplay/network/HotspotJoinElement.kt)标为实验验证，并区分本地连接与互联网验证。这不能证明修改该 IE 会让 iOS 使用 AP 上网。本次没有复制 DiPlay 实现。 |
| 既有源码行为 | 现有 Device IE 为 `DD 08 00 A0 40 00 00 02 00 22`。iAP2 Wi-Fi 请求/配置消息 `0x5702` / `0x5703`承载既有关联配置，其使用不能证明互联网能力通告。 |
| 宿主兼容方案 | 已实现经过检查的 DHCP 通告和限定接口的 NAPT，不臆造未公开的 Networking IE、Device IE 位或 iAP2 互联网消息。iOS 是否还需要其他机制、是否继续优先使用蜂窝网络，仍是明确待验证的互操作问题。 |

用户反馈此前 V2 版本无线 CarPlay 实机正常，有线投屏有显示但卡顿；有线音乐可以辨认但音质较差，主观上比无线音频稍好。这是用户反馈的旧版本证据，没有提供镜像哈希或原始日志，不是互联网共享修改的验收。有线卡顿和音质问题继续作为已知问题保留，本任务不修改相关算法。

## 配置与构建

实现由 `RTCARPLAY_INTERNET_SHARE` 控制，要求 Realtek 应用 Wi-Fi 适配、`RT_USING_LWIP212`、`RT_LWIP_DHCP`、`LWIP_USING_DHCPD`、`AIC_USING_GMAC0`，并关闭 `LWIP_NO_RX_THREAD`。GUI 配置恢复标准 RT-Thread Ethernet RX Worker，因为 GMAC 中断需要 `eth_device_ready`；既有 Wi-Fi 直接输入和直接 TX 路径继续可用，不改变网络结构体布局。`RTCARPLAY_INTERNET_WAN` 默认为 RT-Thread Ethernet 设备名 `e0`，AP 通过既有 WLAN 设备所有者识别，不猜测网卡枚举顺序。D213EC-DEMO V4.0 板级方案使用 MDIO 地址为 1 的 RTL8201F，复位脚为 `PE.6`；RMII 的外部 50 MHz MAC 时钟由 PHY 经 PE3 提供，PHY 自身另需 PE10/CLK_OUT2 提供 25 MHz 参考时钟。GUI defconfig 显式启用 CLK_OUT2，频率为 25000000 Hz。板级构建会拒绝缺失该参考时钟或启用冲突 I2S0/DVP 引脚的 GMAC0 配置。[Ethernet PHY 验证](ethernet-phy-verification.md)记录了网口灯不亮的反馈、原理图依据和复测步骤。实际链路协商和 DHCP 仍需开发板确认。

```sh
bash .ci/build-firmware.sh gui
```

脚本选择既有 `d21x_demo128-nand_rt-thread_rtcarplay_defconfig`，先构建同源码树裸机 Bootloader，再编译应用并检查完整 IMG。保留既有 GUI/Headless CI 矩阵。选择方案后的本地增量构建使用 `scons -j8`。

`RTCARPLAY_INTERNET_PROBE_HOST` 默认为 `connectivitycheck.gstatic.com`，`RTCARPLAY_INTERNET_PROBE_PATH` 默认为 `/generate_204`。应按部署网络配置可访问的 HTTP 204 端点。例如默认域名被屏蔽时，即使部分互联网网站可用，共享也会主动保持关闭。端点和 DNS 运营者会收到相应请求。检测成功只证明该端点可达，不证明所有互联网网站可用、响应具有密码学真实性或 iPhone 已采用该路由。

## 所有权与恢复

应用 Worker 在 TCP/IP Core 上获取 WAN 状态副本，解析检测域名，将 HTTP Socket 绑定到复制的 Ethernet 地址，并要求完整 HTTP 204 响应。非阻塞连接、发送/接收超时及响应总时限避免在 CarPlay 和 TCP/IP Worker 上等待 Socket。代数令牌拒绝旧链路/地址/DNS 状态下的检测结果。每次检测后等待 15 秒再检查。Core 定时器每秒检查接口，正向证据在 45 秒后过期；链路断开及地址/掩码/网关不匹配也会直接在包策略中阻止转发。

复用既有 Ethernet DHCP 客户端和 AP DHCP 服务，共享启动不会重启 SDK 正在协商或已获取租约的 DHCP 客户端。经过检查并启用转发后，AP DHCP 通告 AP 为路由器，当前 IPv4 上游解析器为 DNS。DNS 包经过普通限定接口 NAPT，支持 UDP 和 TCP，不再增加 DNS 代理。解析器路由指向 AP/USB、解析器为空或检测失败时关闭共享。上游不可用时继续分配本地地址，但不通告路由器和 DNS。共享配置使用 60 秒租期。既有 iPhone 路由/DNS 可能保留到租约续期，不能保证 iOS 立即切回蜂窝或恢复后立即采用新出口。状态变化不会重启 AP 或 Receiver。

TCP/IP Core 独占 NAT 状态、网络快照及 DHCP 选项生成。只允许配置的 AP 与 Ethernet 之间转发，USB、其他接口、AP 子网之外的伪造源地址和未经请求的 WAN 到 LAN 流量不能使用该路径。回程转发必须带有本地产生的反向转换标记，复用的转换器负责调整 IP 和传输校验和。绑定源地址的本地流量以及 AP/USB 子网优先于 Ethernet 默认路由，既有 mDNS 显式选择 AP 接口。地址变化或接口退休会在复用前清除映射。

仅共享配置启用的 Core 锁也将 RT-Thread netdev 直接操作及网络接口移除与包处理串行化。转换器启用状态存放在 `struct netif` 之外，保留预编译网络驱动所依赖的既有二进制布局。接口关闭、物理链路断开和移除会立即解除转换器，后续定时刷新同步应用状态。

```text
internet_share
ifconfig
carplay_connection status
carplay_status
```

共享命令报告 Ethernet 链路/地址、检测/共享状态及出向/入向/拒绝 NAT 计数，并将 iPhone 路由标为未验证。链路/地址与检测成功属于不同观察。上游中断但物理链路未断开时，共享关闭可能等待下一次失败检测或最长 45 秒的证据时限。

## NAPT 复用与限制

三个转换器文件取自 [Espressif lwIP](https://github.com/espressif/esp-lwip/tree/f5af4d8b1b4cd4585322f43833beff52b4a189ea) 提交 `f5af4d8b1b4cd4585322f43833beff52b4a189ea`：`ip4_napt.c`、`ip4_napt.h` 和 `lwip_napt.h`，保留 BSD 许可证和版权声明。移植移除 ESP 专属构建/统计依赖，分配失败时不触发断言，允许接口关闭后清理映射，并采用 UDP 120 秒、ICMP 30 秒空闲默认值。应用使用 128 项表，禁用端口映射。新增接口限定策略与测试独立于转换器，未修改无关的备用 lwIP 包。

本实现提供 IPv4 TCP/UDP 和 ICMP Echo 转换，不是 IPv6 路由器、防火墙产品或通用 ICMP 错误转换器。IP 分片、IPv4 选项、错误/截断传输头和不支持的协议会被拒绝。链式 WAN 回包在转换前合并，出向头部连续长度不足时拒绝。共享策略不对超大包分片，DF 包在转换前使用既有 lwIP 产生需要分片响应。NAT 下的远端 ICMP 错误/PMTU、多客户端 ICMP 标识冲突、长期空闲应用、表压力、QUIC 和实际 WAN MTU 仍需进一步验证，不能从小规模 Host 测试推断所有互联网应用均通过验收。

## 可执行软件检查

```sh
make -C application/rt-thread/rtcarplay -j8 check
# The sharing tests can also run independently.
cmake -S application/rt-thread/rtcarplay/tests/internet_share -B build/internet-share -DCMAKE_BUILD_TYPE=Debug
cmake --build build/internet-share -j8
ctest --test-dir build/internet-share --output-on-failure
```

路由测试在 ASan/UBSan 下执行实际生产 lwIP Core、实际转换器和实际接口策略，通过 IPv4 Input 注入 TCP/UDP/ICMP 包，检查转换地址、回程投递、IP/传输校验和、TTL、源/子网路由、错误头、接口隔离、源地址伪造、映射清理及子网冲突。生成的 `internet-routing.pcap` 包含模型原始 IP 入包和实际 lwIP 输出，也包含被拒绝的测试输入。这是软件证据，不是开发板或 iPhone 流量。CI 保留该文件并构建既有 GUI 和 Headless 方案，分别覆盖共享开启和关闭。

监督器测试编译实际应用源码，使用真实 lwIP 状态和模型 RT-Thread/Socket 边界，检查 DHCP 路由器/DNS 字节、离线省略、绑定 WAN 检测、分段响应、登录页/重定向拒绝、超时/错误、过期结果、链路/地址变化、证据到期、AP 停止、错误 DNS 路由和 Worker 分配失败。既有实际源码连接/媒体/USB 测试继续包含在同一检查命令中。CI 定义已在本地更新，发布并实际执行前不宣称远端 CI 通过。

## 必需的 iPhone 验收

| 检查 | 必需证据 | 当前状态 |
| --- | --- | --- |
| Ethernet | PHY/链路、DHCP 租约/地址/网关/DNS，以及绑定出口的成功检测 | PARTIAL：用户确认 [PHY 修复](ethernet-phy-verification.md)后网口灯亮、车机联网正常；DHCP 详情和原始检测日志尚未记录 |
| iPhone 互联网路由 | 准确镜像 SHA256、iPhone/iOS 版本、关闭蜂窝、访问受控公网服务器的唯一请求随机标识、服务端响应/日志，以及显示开发板 WAN 源地址和回程流量的 Ethernet 抓包 | 功能 PASS（用户反馈）：iPhone 可以通过车机网络使用网络服务；详细路由、蜂窝设置和抓包证据尚未记录，参见[实机反馈](ethernet-phy-verification.md)。 |
| 无线兼容 | 同时运行 CarPlay 音视频/触摸和互联网流量、重连、长时运行及 LAN/WAN 计数 | NOT_RUN |
| 恢复 | 拔插网线、DHCP 地址变化、链路保持时网关断网、DNS 失败、检测恢复和租约续期观察 | NOT_RUN |
| 有线回归 | 有线投屏、既有卡顿/音质基线、USB 路由隔离和自动单会话行为 | 本次源码为 NOT_RUN |

在镜像 Ethernet 端口或抓包主机上替换下例中的真实 WAN 和受控服务器地址。受控测试期间关闭 iPhone 蜂窝和其他无关路由，在保持 CarPlay 连接时用 iPhone 加载带唯一随机标识的服务器 URL，关联响应、服务端日志及抓包。仅由开发板发起的检测不能证明 iPhone 路由。保留原始抓包和串口日志，测试后恢复手机正常设置。

```sh
sudo tcpdump -ni <capture-interface> -s0 -w iphone-wan.pcap 'host <board-wan-ip> and host <controlled-server-ip>'
sha256sum <flashed-image.img> iphone-wan.pcap
```

如果手机继续使用蜂窝、拒绝该路由或续期后失败，应先记录 iOS 网络状态和 Beacon/iAP2 抓包，再提出协议修改。新增互联网标志、Networking IE 或 iAP2 消息需要可信规范或可重复设备证据。保持既有 SDK/平台边界，不通过修改音频算法调查路由失败。

手机上 Use For Internet 控件对应的 iAP2 字段仍待确认。
