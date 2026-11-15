[English](../iap2-internet-research.md) | [简体中文](iap2-internet-research.md)

# Use For Internet：iAP2 字段审查

## 审查版本与结论

获取 origin 后审查 DiPlay master `615b32ce0b40cd0b99ce94ba23a512001553684f`，并对照本地 rt-carplay SDK `57936326c311850e006310e94d2aa9d711ba6a1e`。研究日期为 2026-11-14。SDK 保持 main，应用保持 carplay-oss。本次审查未创建分支、PR、推送或改变固件。现有实现与实机验收要求见[互联网共享](internet-sharing.md)。

DiPlay 确实存在所述讨论，但没有提供缺失字段的定义或实现。当前识别、Wi-Fi 配置与会话启动编码器里的网络字段，SDK 已经具备。因此本次审查未确认一个有依据、需要新增的 iAP2 字段。额外互联网声明的消息 ID、参数/组 ID、编码类型、取值和更新规则均为**待确认**。这不证明私有协议没有此类字段，只明确这些来源能够支持的结论范围。

## 源码与报文字节对照

| 消息 / 组 | DiPlay 网络字段 | SDK 结果 |
| --- | --- | --- |
| IdentificationInformation 0x1D01，组 24 / 0x0018 | u16(0,1)、string(1,SSID)、void(2)、u16(3,1)、void(4)、void(5) | 已有；网络组测试报文 39 字节全部一致 |
| AccessoryWiFiConfigurationInformation 0x5703 | 可选 BSSID 0、SSID 1、密码 2、安全类型 u8 3、信道 u8 4 | 已有；含 BSSID 的测试报文 54 字节全部一致 |
| CarPlayStartSession 0x4301，无线组 1 | SSID 0、密码 1、信道 u8 2、可重复 IP 字符串 3、安全类型 u8 4 | 已有；单地址测试报文 60 字节全部一致 |

对照限定于网络字段，不代表整个识别消息完全等价。DiPlay 还有其他可选功能，位置、车辆数据、SDK 版本、仪表资源或双向认证字段，不会因其可选就具有互联网语义。WiFiSharing 分类名称本身不声明互联网路由。不据此推断源码没有说明的数值含义。

本地测试在 ASan/UBSan 下调用 SDK 实际 C 编码器，将输出与固定版本 DiPlay 语句及标量/字符串编码推导的预期字节比较。没有执行 DiPlay Android 应用，不是 iPhone 抓包或手机接受证据；仅使用虚构 SSID、密码和身份。另重新构建并执行了 SDK 识别、CarPlay 消息与会话 Host 测试，3/3 通过。

## Issue 证据与边界

Issue #393 中，社区评论建议在 iAP2 握手补 CarPlay-Network 声明，但没有消息 ID、参数 ID、编码类型或抓包。另一位评论者建议先在 iPhone 点击 Check For Internet Access。随后提问者贴出包含 Use For Internet 的截图，并于 2026-10-07T09:39:23Z 自行关闭 Issue；关闭事件没有关联提交。该时间线不能证明界面出现的原因，也没有记录补字段的代码变更。截图只能证明界面状态，不能证明 Ethernet 路由或具体协议机制。

Issue #657 中，维护者解释 CarPlay 会话未改变时，iPhone 可能将看似可联网的车机热点选作互联网出口。这是维护者对该反馈的分析，不是官方协议定义或某个 TLV 的证据。两处讨论均不足以支持编造常开互联网标志、复用无关参数或修改现有 Apple vendor IE。

## 集成决策与后续证据

保持当前 iAP2 消息及应用持有的 Ethernet/AP/DHCP/DNS/NAPT 策略，不因 Ethernet 有链路或地址就宣告互联网可用。Launcher HTTP 探测检查车机本身，与 iPhone 设置中的 Check For Internet Access 是不同操作；点击一方不代表调用另一方。

新增字段前，需要可核实的字段定义，或同一已知正常车机在互联网不可用/可用、iPhone 开关关闭/开启时的对照抓包。记录准确 iOS 版本、手机界面状态及时间，对照 0x1D01、0x5703、0x4301、后续 iAP2 消息、AP Beacon/Probe 中 Apple Device IE / Networking IE、DHCP 选项、IPv6 路由通告及连通性探测。这些是候选观测点，不是已确认要求。对外分享的抓包须去掉凭据与认证材料。

后续有依据的可移植 SDK 扩展只负责编码已确认字段，由宿主显式提供输入；应用负责 Ethernet/lwIP 就绪、旧结果拒绝与离线恢复。iPhone 流量实际通过 Ethernet 仍须关闭蜂窝数据、请求带唯一标记的服务端并关联 WAN 抓包/服务端日志。此交付实机测试及 Use For Internet 的出现仍为 NOT_RUN。本轮未构建新镜像，既有 Launcher 网络镜像保持不变。

## 本地研究证据

完整克隆保存在 `C:/Project/Carplay/references/DiPlay`。本地证据位于 `C:/Project/Carplay/references/diplay-internet-review`，包含原始 Issue JSON/时间线、已检查截图、actual-sdk-wire.txt、comparison.json、wire-audit.c、compare-wire.py、run-audit.sh 和 SDK 构建/测试日志。比较仅覆盖上表；独立的源码推导测试属于研究证据，不是新增生产测试框架。

## 固定版本引用

- [IdentificationInformation](https://github.com/shihabal3amri/DiPlay/blob/615b32ce0b40cd0b99ce94ba23a512001553684f/shared/src/main/java/com/shilapi/xcertplay/transport/Iap2IdentificationClient.kt#L371).
- [Wi-Fi and CarPlay message codecs](https://github.com/shihabal3amri/DiPlay/blob/615b32ce0b40cd0b99ce94ba23a512001553684f/shared/src/main/java/com/shilapi/xcertplay/iap2/message/Iap2Messages.kt#L77).
- [Scalar and string wire codecs](https://github.com/shihabal3amri/DiPlay/blob/615b32ce0b40cd0b99ce94ba23a512001553684f/shared/src/main/java/com/shilapi/xcertplay/iap2/wire/Iap2WireCodec.kt#L13).
- [Issue #393](https://github.com/shihabal3amri/DiPlay/issues/393).
- [Issue #657 owner comment](https://github.com/shihabal3amri/DiPlay/issues/657#issuecomment-6118129967).
