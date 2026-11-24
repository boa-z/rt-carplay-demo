[English](wifi-concurrent.md) | [简体中文](zh-CN/wifi-concurrent.md)

# RTL8733BS concurrency

The D213 GUI selects `REALTEK_FULL_FNC_MODE` and links `libwlan_rtl8733_full_c906fd.a`. The cross-toolchain `riscv64-unknown-elf-nm -A` reports `rltk_wlan_init`, `rltk_wlan_start`, `rltk_wlan_running` and `rltk_wlan_send_skb` in `rtk_wlan_if.o`, with an imported `rltk_wlan_set_netif_info`. The SDK `wifi_on(RTW_MODE_STA_AP)` initializes two indexes and the full-function headers enable concurrent mode. This establishes the build-time API path. Firmware operation under dual-role traffic remains **NOT_RUN**.

| Radio role | Index 0 | Index 1 |
|---|---|---|
| AP only | AP | Unused |
| STA only | STA | Unused |
| Concurrent radio mode | STA | AP |

The adapter uses the radio index for TX, RX and MAC state, maintains per-device lwIP attachment, guards opposite-role requests from stopping the active interface, and selects the AP interface for the vendor IE. The GUI ships with AP-only CarPlay and explicit STA-only maintenance. Transitioning an active AP to simultaneous STA, soft AP stop within concurrent mode, SCC coordination, subnet overlap handling and Wi-Fi STA Internet Sharing are pending. The normal path does not interrupt projection to change channels. The initial concurrency acceptance target is SCC on 5 GHz channel 36.

| Board run | Procedure and acceptance | Result |
|---|---|---|
| AP only | Boot with STA maintenance disabled. Pair an iPhone, reconnect after disconnect, verify AP DHCP, mDNS, iAP2 and AirPlay, and record `carplay_connection status` plus `carplay_perf`. | NOT_RUN |
| STA only | Enter STA maintenance explicitly, scan and connect. Verify association and DHCP separately. Run `carplay_net status` and `carplay_ota source test`. Reboot the router, then verify reconnection and finally exit maintenance and reconnect CarPlay. | NOT_RUN |
| SCC | When a dual-role bring-up path is available, connect both links to 5 GHz CH36. Capture both netifs while making an HTTPS request through STA. Verify DHCP and mDNS stay on AP, TX/RX counters advance independently and AP/STA subnets do not overlap. | NOT_RUN |
| Media and faults | Repeat the SCC run during 60 FPS projection, music, Siri and calls. Capture frame cadence, audio underruns, bandwidth and link quality. Change or restart the router and verify projection stays active. | NOT_RUN |
| WAN and sharing | Insert/remove Ethernet, use `carplay_net route auto|ethernet|wifi`, and verify stale transfers are invalidated. Verify board HTTPS separately from iPhone NAPT use, with phone cellular disabled and capture on the selected upstream. | NOT_RUN |
