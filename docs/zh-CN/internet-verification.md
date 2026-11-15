[English](../internet-verification.md) | [简体中文](internet-verification.md)

# 互联网共享验证

## 源码与范围

本报告覆盖 `carplay-oss` 上尚未提交的本地互联网共享修改，基于 `b933ef756afd6c9498d99d016482693b45f048b0`。仅基础提交不能标识这些固件镜像。本地交付目录 `output/internet-validation/` 包含修改源码归档、二进制补丁、逐文件源码哈希、配置副本、匹配的 Bootloader、镜像布局、原始构建/测试日志和产物哈希。本任务没有创建分支或 PR，没有推送或修改远程仓库。

可移植 SDK 保持 `57936326c311850e006310e94d2aa9d711ba6a1e`，IPHETH 保持 `2c87b69fda0d2dbeeb9df5180dd10157791d7371`，两个子模块均未修改。应用/网络实现及协议证据见 [Ethernet 互联网共享](internet-sharing.md)。

## 软件检查

| 检查 | 结果 | 证据与限制 |
| --- | --- | --- |
| 实际生产 lwIP 路由和应用监督模块 | PASS | 两个 CTest 可执行程序在 ASan/UBSan 下运行，包括 TCP/UDP/ICMP 往返、校验和、接口隔离、分配失败、过期检测拒绝、HTTP/DHCP 策略和恢复。监督模块的 RT-Thread/Socket 边界使用模型。 |
| 既有应用回归测试 | PASS | 完整 Host 命令执行实际源码的连接、Launcher、媒体、音频测试、配对、视频交接和 USB 检查，不能证明音质或开发板显示效果。 |
| 同源码裸机 Bootloader | PASS | 构建完成，两个固件方案均携带匹配的二进制文件。 |
| 双语文档与静态检查 | PASS | 成对检查标题/代码/技术字面量/链接/段落，以及 SConscript AST、Shell 语法和 Workflow YAML/矩阵。 |
| 既有 GUI 完整固件，Ethernet 和共享开启 | PASS | 本地交叉构建、已链接 NAPT 符号，以及 9 个 IMG 组件边界/CRC 核验。 |
| Headless 完整固件，共享关闭 | PASS | 本地交叉构建、通用 Headless 示例目标文件、无 NAPT 符号，以及 9 个 IMG 组件边界/CRC 核验。 |
| 远程 CI | NOT_RUN | 仅本地更新 Workflow 定义，未触发远程 Workflow。 |

```sh
make -C application/rt-thread/rtcarplay -j8 check
cmake --build /home/boa/build-carplay-internet -j8
ctest --test-dir /home/boa/build-carplay-internet --output-on-failure
```

Host 路由捕获 `output/internet-validation/internet-routing.pcap` 包含模型入包和实际 lwIP 出包，不是开发板 Ethernet 抓包。CI 定义保留对应 Host 抓包并沿用既有 GUI/Headless 矩阵，不增加额外 Internet defconfig 或固件变体。

## 镜像身份

既有 GUI defconfig 直接在前一版本上启用 Ethernet 和可选共享，Headless 继续作为关闭共享的兼容回归。每个本地产物目录包含 `firmware.config`、`bootloader.bin`、`image-layout.json` 和 `SHA256SUMS`。各方案分别生成配置文件，并依次编译。

交叉构建在 WSL Linux 文件系统的源码副本 `/home/boa/carplay-internet-verify` 中运行，以避免 Windows 挂载目录的构建开销。所有修改源码均已与指定检出目录逐字节核对。第一次 GUI 链接暴露 Ethernet RX Worker 被关闭，恢复 SDK Worker 后解决了依赖，无需新增接收实现。最终构建均成功完成。临时单独 Internet 配置已经移除，其中断构建不属于交付产物。指定检出目录最终选择 GUI 配置。生成的已跟踪 Bootloader 在保存匹配构建副本到产物后恢复。

| 方案 | IMG SHA256 |
| --- | --- |
| 启用 Ethernet 共享的 GUI | `068087bd4a0536385f58badad3053b2e82013dd91368068031e482c46bc5a96d` |
| Headless | `1335f773c7e8716b98294f1eb27f82647f26d39ad5d110a4d0a90282514c24c3` |

## 硬件验收与剩余工作

当前源码的 Ethernet PHY 配置、开发板 DHCP/检测、iPhone 实际互联网路由、媒体/触摸并发负载及网线/上游中断恢复均为 **NOT_RUN**。本任务期间未发现可访问的开发板串口或烧录配置。验收流程要求关闭蜂窝后的 iPhone 流量、向受控公网服务器发送唯一请求，并关联 Ethernet 抓包与服务器日志，详见使用指南。软件中的标准 DHCP/NAPT 功能不能证明 iOS 选择该路由，也不能证明不需要私有能力机制。

用户对前一版本的反馈继续作为基线报告：无线 CarPlay 正常，有线投屏有显示但存在已知卡顿，有线音乐可以辨认但音质较差，略好于无线音频。该反馈未附源码/镜像哈希或原始日志。本实现不修改音频算法、媒体/会话代码或 Apple IE/iAP2 行为，也不宣称修复既有音质/卡顿问题。

不支持 IPv4 分片/选项、IPv6 共享和通用远端 ICMP 错误转换。实际 MTU/PMTU 行为、QUIC、映射表压力、长期空闲连接和多客户端 ICMP 标识仍需进一步验证。DHCP 路由器/DNS 变化可能要到续租才能到达手机。可配置的 HTTP 204 检测可能被屏蔽或伪造，不是对整个互联网的认证测试。
