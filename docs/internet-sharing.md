[English](internet-sharing.md) | [简体中文](zh-CN/internet-sharing.md)

# Ethernet Internet sharing

## Scope and evidence

This optional IPv4 gateway uses D213 Ethernet as WAN and the existing CarPlay Wi-Fi AP as LAN. It is implemented in the host application and the production RT-Thread lwIP 2.1.2 integration. The portable rt-carplay SDK, iAP2, Apple vendor IE, session arbitration and audio/video algorithms are unchanged. Ethernet and sharing are added directly to the existing GUI configuration; Headless remains a separate compatibility regression with sharing disabled. The Kconfig switch can disable sharing independently of Ethernet. No additional Internet defconfig is maintained, and enabling the feature is not an iPhone interoperability certification.

See the [local verification report](internet-verification.md) for executed checks, firmware identities and hardware status.

| Evidence class | Finding |
| --- | --- |
| Public standards | IPv4 routing, NAPT, DHCP router/DNS options and normal DNS over UDP/TCP provide a standard gateway path. See [RFC 3022](https://www.rfc-editor.org/rfc/rfc3022) and [RFC 2132](https://www.rfc-editor.org/rfc/rfc2132). |
| Apple public information | [Developing CarPlay Systems, Part 1](https://developer.apple.com/videos/play/wwdc2016/722/) and [Part 2](https://developer.apple.com/videos/play/wwdc2016/723/) describe CarPlay integration; [Apple setup guidance](https://support.apple.com/en-us/108415) describes wireless association. These sources do not establish a public Internet-availability bit or message usable here. |
| Community implementation | DiPlay commit `615b32ce0b40cd0b99ce94ba23a512001553684f` calls its [Apple Device IE policy](https://github.com/shihabal3amri/DiPlay/blob/615b32ce0b40cd0b99ce94ba23a512001553684f/shared/src/main/java/com/shilapi/xcertplay/network/HotspotJoinElement.kt) experimentally verified and separates local connection from Internet validation. It does not prove that modifying that IE causes iOS to use the AP for Internet. No DiPlay implementation was copied. |
| Existing source behavior | The existing Device IE is `DD 08 00 A0 40 00 00 02 00 22`. iAP2 Wi-Fi request/configuration messages `0x5702` / `0x5703` carry the existing association configuration. Their use is not evidence for an Internet capability announcement. |
| Host compatibility design | Checked DHCP offers plus scoped NAPT are implemented. No undocumented Networking IE, Device IE flag or iAP2 Internet message is invented. Whether iOS requires another mechanism or continues to prefer cellular remains an explicit interoperability question. |

The user reports that the preceding V2 build runs wireless CarPlay normally, wired projection displays but stutters, and wired music is recognizable with poor quality, somewhat better than wireless audio. This is user-reported prior-build evidence, without a supplied image hash or raw log; it is not acceptance of the Internet-sharing changes. Wired stutter and the audio-quality problem remain known issues outside this task's algorithm scope.

## Configuration and build

The implementation is gated by `RTCARPLAY_INTERNET_SHARE`, requiring the Realtek application Wi-Fi seam, `RT_USING_LWIP212`, `RT_LWIP_DHCP`, `LWIP_USING_DHCPD`, `AIC_USING_GMAC0` and `LWIP_NO_RX_THREAD` disabled. The GUI configuration restores the standard RT-Thread Ethernet RX worker because the GMAC interrupt requires `eth_device_ready`; the existing direct Wi-Fi input and direct TX paths remain available. This does not change network structure layouts. `RTCARPLAY_INTERNET_WAN` defaults to the RT-Thread Ethernet device name `e0`; the AP is identified by the existing WLAN device owner rather than guessed from interface ordering. The D213EC-DEMO V4.0 board profile uses RTL8201F at MDIO address 1, reset on `PE.6`, RMII with the PHY supplying the external 50 MHz MAC clock on PE3, and a separate required 25 MHz PHY reference from PE10/CLK_OUT2. The GUI defconfig explicitly enables CLK_OUT2 at 25000000 Hz. The board build rejects GMAC0 without that reference or with conflicting I2S0/DVP pins. See [Ethernet PHY verification](ethernet-phy-verification.md) for the reported dark-port failure, schematic evidence and retest procedure. Actual link negotiation and DHCP still require board confirmation.

```sh
bash .ci/build-firmware.sh gui
```

The helper selects the existing `d21x_demo128-nand_rt-thread_rtcarplay_defconfig`, builds the same-tree baremetal Bootloader, builds the application and validates the complete IMG. The existing GUI/Headless CI matrix is retained. Incremental local builds after selecting the profile use `scons -j8`.

`RTCARPLAY_INTERNET_PROBE_HOST` defaults to `connectivitycheck.gstatic.com`; `RTCARPLAY_INTERNET_PROBE_PATH` defaults to `/generate_204`. Configure a reachable HTTP 204 endpoint for the deployment network. For example, a network that blocks the default host will deliberately keep sharing closed even when other Internet destinations work. The endpoint and DNS operator receive the corresponding requests. A successful probe proves reachability to that endpoint, not all Internet sites, cryptographic authenticity, or iPhone routing.

## Ownership and recovery

The application worker requests copied WAN state on the TCP/IP core, resolves the probe host, binds the HTTP socket to the copied Ethernet address and requires a complete HTTP 204 response. Nonblocking connect, send/receive timeouts and a response deadline keep socket waits off CarPlay and TCP/IP workers. A generation token rejects results from an earlier link/address/DNS generation. Checks repeat after a 15-second delay. A core timer checks interfaces every second and expires positive evidence after 45 seconds; link-down and address/mask/gateway mismatches also block forwarding directly at the packet policy.

The existing Ethernet DHCP client and AP DHCP server are reused; a pending or bound SDK DHCP client is not restarted by sharing startup. With checked forwarding enabled, AP DHCP offers advertise the AP as router and the current IPv4 upstream resolver as DNS. DNS packets use ordinary scoped NAPT, supporting UDP and TCP without another DNS proxy. A resolver routed through AP/USB, an empty resolver or invalid probe closes sharing. On unavailable upstream, offers retain local addressing but omit router and DNS. Sharing builds use a 60-second lease. Existing iPhone routes/DNS may persist until lease renewal; immediate iOS cellular fallback or immediate adoption after recovery is not guaranteed. Neither state change restarts the AP or Receiver.

The TCP/IP core exclusively owns NAT state, network snapshots and DHCP option generation. Only the configured AP-to-Ethernet pair is forwarded. USB, other interfaces, source addresses outside the AP subnet and unsolicited WAN-to-LAN packets cannot use that path. Return forwarding requires a local reverse-translation marker; IP and transport checksums are adjusted by the reused translator. Source-bound local traffic and AP/USB subnets are routed before the Ethernet default; existing mDNS explicitly selects the AP interface. Address changes and interface retirement clear mappings before reuse.

Sharing-only core locks also serialize direct RT-Thread netdev operations and network-interface removal with packet processing. Translator activation is stored outside `struct netif`, preserving its existing binary layout for prebuilt network drivers. Interface-down, physical link-down and removal detach the translator immediately; a later timer refresh synchronizes application status.

```text
internet_share
ifconfig
carplay_connection status
carplay_status
```

The sharing command reports Ethernet link/address, probe/sharing state and outbound/inbound/rejected NAT counters. It labels the iPhone route as unverified. Link/address and probe success are separate observations. A route may close up to the next failed probe or the 45-second evidence limit during an upstream outage without physical link loss.

## Reused NAPT and limits

The three translator files were taken from [Espressif lwIP](https://github.com/espressif/esp-lwip/tree/f5af4d8b1b4cd4585322f43833beff52b4a189ea) commit `f5af4d8b1b4cd4585322f43833beff52b4a189ea`: `ip4_napt.c`, `ip4_napt.h` and `lwip_napt.h`. Their BSD license and copyright notices are retained. Port changes remove the ESP-only build/statistics dependency, handle allocation failure without an assertion, permit mapping cleanup after interface-down, and use 120-second UDP/30-second ICMP idle defaults. The application uses a 128-entry table with port mapping disabled. New interface-scoping policy and tests are separate from the translator. The unrelated alternate lwIP package was not changed.

This is IPv4 TCP/UDP and ICMP echo translation, not an IPv6 router, firewall product or general ICMP error translator. IP fragments, IPv4 options, malformed/truncated transport headers and unsupported protocols fail closed. Chained WAN replies are coalesced before translation; insufficient contiguous outbound headers are rejected. Oversized packets are not fragmented by this sharing policy; DF packets receive the existing lwIP fragmentation-needed response before translation. Remote ICMP errors/PMTU through NAT, multiple-client ICMP identifier collisions, long idle applications, table pressure, QUIC and real WAN MTUs require further validation. Do not treat all Internet applications as accepted from a small Host test.

## Executable software checks

```sh
make -C application/rt-thread/rtcarplay -j8 check
# The sharing tests can also run independently.
cmake -S application/rt-thread/rtcarplay/tests/internet_share -B build/internet-share -DCMAKE_BUILD_TYPE=Debug
cmake --build build/internet-share -j8
ctest --test-dir build/internet-share --output-on-failure
```

The routing test runs the actual production lwIP core, actual translator and actual interface policy under ASan/UBSan. It injects TCP/UDP/ICMP packets through IPv4 input and checks translated source/destination, return delivery, IP/transport checksums, TTL, source/subnet routing, invalid headers, interface isolation, source spoofing, mapping reset and subnet conflict. Its `internet-routing.pcap` contains modeled raw-IP ingress and actual lwIP output, including rejected test inputs. It is software evidence, not board or iPhone traffic. CI preserves it and builds the existing GUI and Headless profiles, covering sharing enabled and disabled respectively.

The supervisor test compiles the actual application source with real lwIP state and modeled RT-Thread/socket boundaries. It checks DHCP router/DNS bytes, offline omission, WAN-bound probing, partial responses, captive/redirect rejection, timeout/error paths, stale results, link/address changes, evidence expiry, AP stop, invalid DNS route and worker allocation failure. The existing actual-source connection/media/USB suite remains part of the same check command. CI definitions are updated locally; no remote CI result is claimed before publication and execution.

## Required iPhone acceptance

| Check | Required evidence | Current status |
| --- | --- | --- |
| Ethernet | PHY/link, DHCP lease/address/gateway/DNS and a successful bound probe | PARTIAL: user confirmed LEDs and board connectivity after the [PHY fix](ethernet-phy-verification.md); DHCP details and raw probe logs remain unrecorded |
| iPhone Internet route | Exact image SHA256, iPhone/iOS version, cellular disabled, unique request nonce to a controlled public server, server response/log and Ethernet packet capture showing the board's WAN source and return traffic | Functional PASS (user report): iPhone can use Internet services through the vehicle network; detailed route/cellular/capture evidence remains unrecorded. See [hardware feedback](ethernet-phy-verification.md). |
| Wireless compatibility | Concurrent CarPlay video/audio/touch and traffic, reconnect, prolonged use and LAN/WAN counters | NOT_RUN |
| Recovery | Cable removal/reinsertion, DHCP address change, gateway outage with link up, DNS failure, restored probe and observed lease renewal | NOT_RUN |
| Wired regression | Wired projection, existing stutter/poor audio baseline, USB route isolation and automatic single-session behavior | NOT_RUN for this source |

On a mirrored Ethernet port or an Ethernet capture host, use the following example after substituting the actual WAN and controlled server addresses. Disable iPhone cellular and unrelated routes for the controlled test, load a unique server URL nonce from the iPhone while CarPlay remains connected, and correlate the response, server log and capture. A board-originated probe alone is not iPhone route proof. Preserve raw captures and serial logs, and restore normal phone settings after testing.

```sh
sudo tcpdump -ni <capture-interface> -s0 -w iphone-wan.pcap 'host <board-wan-ip> and host <controlled-server-ip>'
sha256sum <flashed-image.img> iphone-wan.pcap
```

If the phone keeps using cellular, rejects the route or fails after renewal, record the iOS network state and beacon/iAP2 captures before proposing a protocol change. An authenticated specification or reproducible device evidence is required before adding an Internet flag, Networking IE or new iAP2 message. Retain the existing SDK/platform boundary and do not change audio algorithms to investigate a routing failure.

See the [Use For Internet iAP2 field audit](iap2-internet-research.md) for the pinned DiPlay comparison and the unresolved field definition.
