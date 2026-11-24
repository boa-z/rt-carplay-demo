[English](../network-manager.md) | [简体中文](network-manager.md)

# 网络管理器

应用优先选择以太网，其次选择已关联并取得 IPv4 地址和网关的 Wi-Fi STA。CarPlay AP 和 USB NCM 属于本地接口。默认路由由 TCP/IP 核心管理。`carplay_net status` 显示路线和 DNS 配置状态。`carplay_net route auto|ethernet|wifi` 为本次启动显式选路。指定出口不可用时，默认路由为空，直到链路恢复或修改策略。

OTA 保留接口及 IPv4 地址、掩码、网关和 DNS 快照，将 HTTP 和 HTTPS 套接字绑定到选中的 lwIP 接口。只有 IPv4 地址、没有网关的接口也可访问同网段 HTTP 镜像。快照变化会使传输失效。[有线共享](internet-sharing.md)的探测独立于 OTA。lwIP 当前使用全局 DHCP DNS 表，第二个 WAN 租约可能覆盖 DNS 并使 OTA 失效。

在**系统设置 → 日期与时间 → 校时服务器**保存主机名或 IPv4 地址，重启后仍有效。**恢复默认**选用固件的 `RTCARPLAY_NTP_SERVER`，当前默认是 `time.cloudflare.com`。**联网校时**使用点击时选中的服务器发起一次 SNTP 请求。同网段 IPv4 校时服务器也可通过没有网关的接口访问。RTC 写入 UTC，保留当前 UTC 偏移和时钟模式。

## 仪表与有线网络设置

仪表顶部时钟旁分别显示 Wi-Fi STA 和有线网络图标。断开时图标变暗，已连接但等待 IPv4 时使用灰色，取得 IPv4 地址后高亮。**系统设置 → 有线网络**显示网线及 IPv4 状态、地址、子网掩码、默认网关和当前系统 IPv4 DNS 服务器。断线后清空地址字段。网络管理器按需启动有线 DHCP，并复用已运行的客户端。

## STA 维护

在 Settings → Wi-Fi 中点击 **Pause CarPlay and enable Wi-Fi**，明确请求 STA 独占维护。连接管理器关闭会话、停止接收端并禁止新会话，然后由 Wi-Fi 工作线程启动 STA。扫描、选择接入点并输入密码。退出维护后恢复 CarPlay 连接策略。无线 CarPlay 的持久开关独立于维护模式。

界面显示扫描信号、频段、安全类型、关联与 DHCP 状态。维护期间以 RAM 中最后一次连接成功的凭据重试。点击 **Forget network** 清除工作线程保存的凭据并断开。GUI 方案关闭 Realtek 明文配置写入。受保护的凭据持久化及多网络管理仍待实现。参见[并发验证方案](wifi-concurrent.md)。
