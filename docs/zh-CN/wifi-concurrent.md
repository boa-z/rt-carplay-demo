[English](../wifi-concurrent.md) | [简体中文](wifi-concurrent.md)

# RTL8733BS 并发

D213 GUI 选用 `REALTEK_FULL_FNC_MODE` 并链接 `libwlan_rtl8733_full_c906fd.a`。交叉工具链的 `riscv64-unknown-elf-nm -A` 显示 `rtk_wlan_if.o` 包含 `rltk_wlan_init`、`rltk_wlan_start`、`rltk_wlan_running`、`rltk_wlan_send_skb`，并引用 `rltk_wlan_set_netif_info`。SDK 的 `wifi_on(RTW_MODE_STA_AP)` 初始化两个索引，Full Function 头文件启用并发。这些证据确认构建路径。双角色流量下的驱动库行为为 **NOT_RUN**。

| 射频角色 | 索引 0 | 索引 1 |
|---|---|---|
| AP Only | AP | 未使用 |
| STA Only | STA | 未使用 |
| 并发射频模式 | STA | AP |

适配层按射频索引选择收发路径及 MAC 状态，lwIP 协议归属按设备管理，阻止另一角色误关闭活动接口，Vendor IE 使用实际 AP 接口。GUI 提供 AP Only CarPlay 和显式 STA Only 维护模式。AP 活动时切入并发、并发下单独停止 AP、SCC 信道协调、子网冲突处理和 STA Internet Sharing 仍待实现。常规路径不会重置信道来打断投影。并发首轮上板目标为 5 GHz CH36 的 SCC。

| 实机项 | 操作及判断依据 | 结果 |
|---|---|---|
| AP Only | 关闭 STA 维护，配对并重连 iPhone。验证 AP DHCP、mDNS、iAP2、AirPlay，记录 `carplay_connection status` 和 `carplay_perf`。 | NOT_RUN |
| STA Only | 明确进入维护、扫描和连接，分别确认关联及 DHCP。执行 `carplay_net status`、`carplay_ota source test`。重启路由器，验证 STA 恢复，最后退出维护并重连 CarPlay。 | NOT_RUN |
| SCC | 双角色入口具备后，将两条链路设为 5 GHz CH36，同时在两接口抓包并通过 STA 访问 HTTPS。验证 AP DHCP/mDNS、独立收发计数及子网不冲突。 | NOT_RUN |
| 媒体及故障 | SCC 下覆盖 60 FPS、音乐、Siri、电话和路由器重启或换信道，记录帧率、音频欠载、带宽、链路质量及投影连续性。 | NOT_RUN |
| 出口与共享 | 插拔网线并切换 `carplay_net route auto|ethernet|wifi`，验证旧传输失效。分别验证车机 HTTPS 与手机 NAPT，手机侧关闭蜂窝网络并在出口抓包。 | NOT_RUN |
