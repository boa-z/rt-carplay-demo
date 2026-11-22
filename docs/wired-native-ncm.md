[English](wired-native-ncm.md) | [简体中文](zh-CN/wired-native-ncm.md)

# Wired CarPlay over native CDC-NCM

## Maintained configuration

The D213 GUI defconfig uses `RTCARPLAY_NATIVE_NCM=y` and `RTCARPLAY_WIRED_NETIF="n0"`, with both `LPKG_USING_CHERRYUSB_IPHETH` and the upstream singleton NCM class disabled. Only one USB network implementation may activate the phone. Kconfig, SCons, a compiler guard and the CI config/ELF check enforce this boundary. No additional maintained defconfig or Headless firmware is introduced. Protocol and network header tracing are disabled in normal builds.

Earlier native candidates still bound Apple Ethernet/IPHETH interface 2 and enabled its session link after pairing while advertising the native NCM interface. Those tests did not isolate native NCM. The accepted profile removes that competing activation. This comparison supports exclusive NCM as the compatibility repair; it does not establish an official Apple requirement or explain the phone's internal policy. The selected NCM MAC identity is retained, but its independent necessity has not been established by a successful controlled comparison.

## Module ownership

| Owner | Responsibility |
| --- | --- |
| rt-carplay | Portable iAP2/AirPlay protocols, identification and session messages; optional synchronous borrowed-body iAP2 observer. No NCM, lwIP, GUI or SoC dependency. |
| Luban application/platform integration | USB configuration selection, exclusive NCM binding, descriptor/NTB codec, RT-Thread worker and lwIP netif, DMA ownership, selected-netif identity, connection policy and diagnostics. |
| cherryusb-ipheth | Independent optional Apple Ethernet class, its vendor framing and generic traffic diagnostics. Not linked in the maintained native profile. |
| CherryUSB host integration | Generic multi-qTD short-IN completion correction, retained independently of the CarPlay handshake. |

`native_ncm_codec.c` is portable transport code, not a CarPlay protocol. It stays beside its bounded application adapter rather than expanding the SDK or introducing another repository. A future general NCM driver can extract it with its tests if other consumers require that API. The adapter uses public CherryUSB and RT-Thread/lwIP APIs; no upstream singleton class is patched or replaced. Media, charging values, StartSession encoding, wireless policy and OTA behavior are outside this change.

## Transport and lifecycle

The adapter resolves the first Apple CDC-NCM function through its CDC Union and Ethernet descriptors, selects the data alternate setting and reads that function's MAC string. It does not hardcode a phone PID, USB configuration number or endpoint address. The second NCM function remains unbound. Before iAP2 starts, its advertised interface must match the initialized native binding. The receiver identity is constructed from the selected NCM MAC before /info, pairing and invitation construction; existing persisted Ed25519 keys are preserved and a running identity is never rewritten.

Supported framing is NTB16/NCM0, Ethernet frames up to 1514 bytes, input NTBs up to 16 KiB, output storage up to 8 KiB and up to 64 datagrams in one unchained NDP. Parameters, alignment and optional filter/CRC controls are checked or negotiated. Unsupported chained NDPs, CRC-bearing frames and incompatible limits are rejected. These are explicit compatibility limits, not claims about every iPhone configuration.

Persistent aligned DMA storage belongs to one asynchronous bulk receiver and an optional notification receiver. Completion callbacks allocate no payload and print no UART output; the worker validates NTBs and copies Ethernet frames into lwIP-owned pbufs. Disconnect stops submissions, drains sends and cancels receivers before releasing the hub port. Failed cancellation quarantines the owner until reboot; that exceptional path is modeled on Host and has not been forced on a board.

Local link readiness permits IPv6 DAD and invitation before the phone opens its data pipe. It is separate from observed NETWORK_CONNECTION notifications and does not imply WAN/Internet availability. DHCP and router autoconfiguration are disabled on this dedicated link. A few bounded initial TX timeouts while the phone NAKs are not by themselves a failed CarPlay session; evaluate actual TCP, authentication and media establishment.

## Diagnostics and validation

`carplay_usb net` reports the selected netif, native interface, RX/TX/error counters, observed carrier and IPv6 state. Optional `RTCARPLAY_NCM_TRACE` retains at most 24 header records; `RTCARPLAY_WIRED_PROTOCOL_TRACE` observes at most 16 selected messages, with raw fields bounded to 128 bytes in short console lines. Neither logs media or authentication key bodies. Observed TX means queued/submitted, not peer acceptance. These switches are off by default and are not prerequisites for connection.

Run `make -C application/rt-thread/rtcarplay check-native-ncm check-lubanlite-usb check-wired-identity check-wired-trace check-iap2-endpoints check-usb-iap2-worker` on Linux. Tests exercise actual codec/worker functions, malformed metadata, two NCM functions, asynchronous ownership and cancellation, native/legacy configuration selection, exclusion of IPHETH providers, consistent receiver identity and preserved keys. ASan/UBSan checks do not replace USB bus or phone acceptance tests. CI also checks the effective GUI configuration and linked ELF so a future build cannot silently reactivate a competing network class.

On 2026-11-15 the user confirmed normal wired operation on iPhone 17e, iPhone 13 and iPhone 13 Pro Max (reported iOS 27.2), using exclusive image SHA256 `6ab86584e4315ce3ee2efec3e40aee78df822bd6d7f4c0538bb4f1a5c602ebda`. The supplied 17e capture, ending `20261115_065722.txt`, reports peer iOS 26.6.2 and confirms exclusive NCM, matching selected netif, RTSP connection, completed /auth-setup, first screen frame, visible video and PCM audio packets. It contains one audio timestamp gap; this is not evidence of lossless playback or a long-duration performance result. Raw captures remain local because they contain device identifiers.

That tested image had the optional diagnostic switches enabled. The maintained release configuration uses the same transport behavior with tracing disabled; a newly built release image has a different identity and must not inherit the exact-image board acceptance. Repeated unplug/replug, long playback, calls/echo, wireless Siri recovery, Internet-sharing and OTA regression on the final release image remain separate board tests. Existing reports of wireless freeze after Siri and wired frame pacing are not closed by this startup repair.
