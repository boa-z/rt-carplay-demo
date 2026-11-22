[English](../ethernet-phy-verification.md) | [简体中文](ethernet-phy-verification.md)

# Ethernet PHY 验证

## 反馈与原因

用户反馈网线和路由器已确认正常，但网口灯不亮，Launcher 显示 Cable disconnected。故障 GUI 镜像启用了 GMAC0，CLK_OUT2 处于关闭状态。启用 CLK_OUT2 后，用户确认网口灯已亮，车机联网正常，后续还验证了 iPhone 可经车机网络使用网络服务。DHCP、蜂窝配置和抓包详情尚未记录。

本地 D213EC-DEMO V4.0 原理图第 1、2、8 页标明 U15 为 RTL8201F，PE6 为复位，PE10/CLK_OUT2 为 EPHY0-25M，PE3 为 MAC0_REFCLK。第 8 页将 25 MHz 参考时钟连接到 PHY，通过电阻配置 RMII 模式、PHY 地址 1 和 TXC 输出。PHY 向 MAC 提供另一条 50 MHz RMII 时钟。仅选择外部 RMII 时钟输入不会启用 PHY 参考时钟输出。已有板级 sys_clk.c 和 pinmux.c 只有在配置 CLK_OUT2 时才会启用 PE10 参考时钟。硬件文档保留为本地参考资料，不复制进公开源码树。

## 修改

原有 GUI defconfig 显式启用 CLK_OUT2，频率为 25000000 Hz，同时明确 RMII、外部 MAC 时钟和 PHY 地址 1。复位仍使用 PE6。新增板级编译保护，拒绝 GMAC0 缺失参考时钟、参考频率错误或启用冲突 I2S0/DVP 引脚的配置。复用 SDK 原有时钟和引脚实现。不增加 defconfig，不修改 SDK 协议、媒体或会话行为。

## 本地验证

| 检查 | 结果 | 依据与边界 |
| --- | --- | --- |
| GUI 实际生效时钟和引脚 | PASS | 使用交付配置和交叉编译器预处理实际板级源码，确认 25 MHz 时钟项及 PE0 至 PE10 唯一 MAC/复位/参考时钟配置。 |
| 配置保护 | PASS | 实际板级源码分别拒绝缺失时钟、50 MHz 参考频率、I2S0 和 DVP。 |
| GUI 和 Headless 完整镜像 | PASS | 本地依次完成 SCons 构建，使用同源缓存 Bootloader，9 个 IMG 组件通过边界和 CRC32 校验。 |
| 生产 lwIP 路由和管理器 | PASS | ASan/UBSan 下两个 CTest 可执行测试，接口和套接字边界使用模型。 |
| 实际 LVGL 渲染和事件 | PASS | 12 张预览及既有网络、设置、设备、音频、配对事件检查，网络数值使用模型。 |
| 网口灯与车机联网 | PASS（用户反馈） | 用户烧录替换 GUI 镜像后确认网口灯亮、联网正常；没有 Agent 独立测量或原始日志。 |
| DHCP 详情与网线恢复 | NOT_RUN | 用户尚未单独记录，当前主机未枚举到串口。 |
| iPhone 通过车机网络使用互联网服务 | PASS（用户反馈） | 用户确认功能可用；关闭蜂窝数据的路由证据、手机/iOS 身份和抓包尚未记录。 |
| CarPlay 并发负载与网络恢复 | NOT_RUN | 当前镜像的长时间音视频/触摸负载、网线/WAN 恢复和有线回归尚未单独反馈。 |
| 远端 CI | NOT_RUN | 此处为本地验证记录，发布后的远端 CI 结果单独跟踪。 |

## 产物身份

替换 GUI 镜像为 output/ethernet-phy-validation/gui/d213-carplay-gui.img，SHA256 为 `5002e942799e8aaf5b3fe3ff827b9d67abd7e8db7f8afa9384517b69cd4d0468`。Headless 镜像 SHA256 为 `1110fe5b33c64c323cbefb2cea665755809055458d039518b158a0bc82b5af6b`。目录包含配置、Bootloader、镜像布局、固件符号、日志、本地源码快照和校验和。镜像在授权提交之前使用已记录的未提交源码快照构建。实现提交 dcb9a339 记录该源码、空白清理和首次实机反馈；本次后续文档更新记录用户的 iPhone 测试成功，以及授权在现有 carplay-oss 分支提交和推送。不涉及创建或切换分支、创建 PR。

## 开发板复测

1. 烧录替换 GUI IMG，记录哈希。插入已确认正常的网线开机，观察网口灯并打开 Launcher Ethernet 页面。
2. 确认链路、DHCP 地址、网关和 DNS，执行 Check now 并记录结果；物理链路与互联网连通性分别验证。
3. 拔出并重新插入网线，确认旧成功状态清除、链路/地址/连通性恢复。在无线和有线 CarPlay 运行中重复测试，按既有基线记录音视频和触摸表现。
4. 如果网口灯仍不亮，采集完整启动日志，重点保留 aicmac 复位超时、PHY MDIO 错误和 Port 0 链路消息。先测量 PE10 是否输出 25 MHz、PE6 复位是否释放、PHY 3.3 V 是否正常，再判断后续是否属于 DHCP 或 iAP2 问题。核对实际开发板版本与原理图。本轮尚未进行这些测量。
