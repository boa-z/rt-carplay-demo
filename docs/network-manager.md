[English](network-manager.md) | [简体中文](zh-CN/network-manager.md)

# Network manager

The application selects Ethernet, then an associated Wi-Fi STA with an IPv4 address and gateway. CarPlay AP and USB NCM are local interfaces. The TCP/IP core owns the default route. `carplay_net status` reports route presence and whether a resolver is configured. `carplay_net route auto|ethernet|wifi` selects the policy for this boot. An explicitly selected unavailable WAN leaves the board without a default route until the link recovers or the policy changes.

OTA reserves the selected interface, records the IPv4 address, mask, gateway and DNS, and binds HTTP and HTTPS sockets to that lwIP interface. A same-subnet HTTP mirror can use an interface with an IPv4 address and no gateway. Changes to the snapshot invalidate the transfer. The [Ethernet sharing](internet-sharing.md) probe is independent of OTA. lwIP currently uses a global DHCP DNS table, so a second WAN lease can overwrite the resolver and invalidate OTA.

**Settings → Date & time → Time server** saves a host name or IPv4 address across reboots. **Restore default** selects the firmware default from `RTCARPLAY_NTP_SERVER` (`time.cloudflare.com` in this configuration). **Sync via network** sends one SNTP request using the server selected when the action starts. A same-subnet IPv4 time server can also be reached without a gateway. The RTC receives UTC and the configured offset and clock mode are preserved.

## Dashboard and Ethernet settings

The dashboard shows separate Wi-Fi STA and Ethernet icons next to the clock. Each icon is dim while disconnected, muted while connected and waiting for IPv4, and accented once an IPv4 address is available. **Settings → Ethernet** shows the cable and IPv4 state, address, subnet mask, default gateway, and the current system IPv4 DNS server. Address fields clear when the link drops. The Network Manager starts Ethernet DHCP when needed and reuses an active client.

## STA maintenance

Settings → Wi-Fi → **Pause CarPlay and enable Wi-Fi** explicitly requests STA maintenance. The Connection Manager closes the current session, stops the receiver and reserves session startup before the Wi-Fi worker enables STA. Scan, select an access point and enter its password. Leave maintenance to restore CarPlay policy. The persistent wireless CarPlay preference is independent of maintenance.

The worker shows scan RSSI, band and security, plus association and DHCP state. It retries the last successful credentials in RAM. **Forget network** clears the worker’s saved credentials and disconnects. The GUI build disables the Realtek plaintext configuration writer. Protected persistent credentials and multiple saved networks are pending. See the [concurrency test plan](wifi-concurrent.md).
