[English](ultra-development.md) | [简体中文](ultra-development.zh-CN.md)

# CarPlay Ultra development and D213 fusion

The final product objective is complete official CarPlay Ultra. This development profile is an intermediate protocol and integration bench. Classic video plus LVGL instruments, Ultra-related negotiation and the official product have separate acceptance criteria. No appearance, popup, Host simulation or second video stream proves complete Ultra support.

## Sources and implementation baseline

Development uses boa-z/luban-lite-carplay on the existing carplay-oss branch; luban-lite-jc-carplay-oss is abandoned. The implementation baseline is integration 7be401243ff9b64eb94a64cfaa082cb6dc398c27 and SDK d0bc5190807279c62d08f78c3c428f3ca8704540. The first SDK foundation milestone is 183fd91167b9d2ca20e1b5f53882d3a6f1bf1ce9. Existing native NCM and short bulk-IN fixes are retained. The parent gitlink identifies the selected SDK revision.

The local LIVI checkout is C:/Project/Carplay/references/LIVI at 64a05b68358db52ed887169143afd5944fd6f1f7. Code and tests, rather than README feature claims, are the research evidence. Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI). GPL-3.0-or-later and its NOTICE attribution requirement apply to inclusion and translation. Original notices remain; startup and Settings include visible attribution. Do not distribute Apple themes, proprietary assets, keys or identity material.

## Classic, Ultra and official product requirements

Apple's [architecture presentation](https://developer.apple.com/videos/play/wwdc2024/10111/) describes a wireless experience, HEVC, Remote UI, Overlay UI, an OpenGL Local UI renderer, authenticated image/script packages, Punch-through UI and presentation-timed UI Sync. Multiple video displays can also exist in Classic. The [CarPlay page](https://developer.apple.com/carplay/), [MFi process](https://mfi.apple.com/en/how-it-works) and [Ultra announcement](https://www.apple.com/newsroom/2025/05/carplay-ultra-the-next-generation-of-carplay-begins-rolling-out-today/) provide official context, not a complete public wire specification or qualification result.

D213 already supplies the MPP H.264 and DE hardware-layer path needed for an application fusion demo. This does not establish its ability to implement the complete official graphics, codec, assets and synchronization requirements. D213 HEVC/OpenGL support, full Ultra interoperability and official product access/qualification remain unverified. Work on portable protocol behavior can proceed while those requirements are investigated; unfinished features stay absent from phone declarations.

## LIVI source findings

All reference paths below start under native/crates/livi-cp/src unless otherwise stated.

| Source | Actual behavior and limitation |
| --- | --- |
| info.rs | Required Main Display Type 110; optional cluster Type 111; physical/pixel geometry, input, view/safe areas and appearance. hevcInfo is conditional. add_display_panels can synthesize an instrument panel without an initial video stream; do not copy that as a verified single-screen capability. |
| stack/setup.rs and stack/control.rs | altScreen follows configured cluster. Vehicle State follows the phone feature offer; UI Sync also requires theme display configuration. control.rs modifies the effective /info result with vehicle/panel/UI Sync information, so info.rs alone is incomplete evidence. |
| stack/data_streams.rs | Type 130 routing by clientTypeUUID; normal Vehicle State 3E2F3C61-AAD0-42CB-A8AA-BF22186DA62E, high lane FF4A6720-F2BE-4F56-A3E1-DB3B4E37D634, UI Sync 07D9F906-8D64-4B54-A808-A20BCA2C51C2. Pair-Verify secret, independent stream identity/port and duplex encrypted tunnels are required. |
| iap_tunnel.rs and session_keys.rs | APTransportPackage assembly, duplex synchronization acknowledgements, HKDF-SHA512 with DataStream-Salt plus the stream seed and directional Input/Output keys. Preserve authentication, peer binding, nonce/key lifecycle, length bounds, fragmentation and teardown. |
| vehicle_state.rs and vehicle_state/wire.rs | Accessory/service/characteristic/control models, OPACK pluginID plus nested pluginData, config/read/write/register/control exchanges, transaction echo and changed subscribed values through updateNotify. Phone writes primarily enforce ownership; universal type/range validation is not demonstrated. No explicit unregister is established. |
| ui_sync.rs | OPACK around binary plist; type/ssn/psn/asn, reset, version negotiation to 2, reset completion, command and ACK; layout/palette/appearance/focus, wrong-session filtering and reopen. This is not proof of a complete presentation-timed compositor, retransmission queue or sequence-window/timeout policy. |
| native/crates/livi-core/src/projection/theme_catalog.rs | Catalogue/package metadata, package.json/theme.json, ZIP parsing, display/layout/palette/zones and cache. JWT payload extraction and metadata parsing alone do not establish complete asset authenticity verification. |

Static inspection found 15 test annotations in info.rs, 27 in vehicle_state/tests.rs and one broad ui_sync.rs test. They cover metadata, configuration/read/register/write/notification behavior, reset/version/layout/ACK/focus/session/reopen, with additional tunnel and synthetic theme tests. LIVI tests have not been executed here. Their presence is not iPhone or board evidence.

## SDK milestone and remaining protocol work

The [portable foundation](../../../../packages/third-party/rt-carplay/docs/ultra-foundation.md) provides optional Main Display profile metadata, aspect-fit/touch geometry, bounded OPACK and a Vehicle State message processor. Host-supplied IDs, ownership, mutability, format/range validation and callbacks avoid inventing a vehicle catalogue. Make runs 57 test programs plus legacy E2E, and CMake runs 58 cases, with sanitizer, public-header, boundary and bilingual-document checks.

Live Type 130 is still the existing receive-only iAP path. The new Vehicle State processor is not yet wired to receiver negotiation or /info; UI Sync is not implemented. Next steps are a bounded duplex registry, authenticated whole-message routing, phone-offer gating, lifecycle/queue failure tests, UI Sync binary-plist state/ACK handling and public receiver status/errors. HEVC, displayPanels, altScreen and unsupported vehicle/UI features are not added by the development flag. SDK code stays independent of LVGL, MPP, DE and D213.

## Dedicated development profiles and artifacts

| Profile | Configuration | Current purpose |
| --- | --- | --- |
| GUI | d21x_demo128-nand_rt-thread_rtcarplay_ultra_gui_defconfig | Existing LVGL launcher and Classic media, explicit Ultra development flag and attribution; fusion is pending. |
| Headless | d21x_demo128-nand_rt-thread_rtcarplay_ultra_headless_defconfig | Automatic Classic connection without LVGL; protocol and lifecycle bench. |

Both preserve exclusive native NCM, n0 identity and disabled diagnostic tracing. The development flag applies an explicit default Main Display profile but advertises no unfinished protocol. Each build first verifies its matching bootloader, checks the wired profile and validates the complete image. The build script restores shared generated configuration and bootloader pack inputs on exit. Windows builds use the existing native toolchain; a WSL build must not replace it with a Linux toolchain.

```sh
bash .ci/build-ultra-demo.sh gui
bash .ci/build-ultra-demo.sh headless
```

The ultra-demo workflow builds both variants after SDK and application Host checks. Downloadable Actions artifacts contain the complete IMG, ELF/map, bootloader, resolved configuration, source/acceptance metadata, logs and SHA256SUMS under output/ultra-ci/gui or output/ultra-ci/headless. These are development images, not qualified Ultra releases. Push milestones use the commit marker [ultra-dev] so the existing rolling Classic Preview Release job skips them; the dedicated build/artifact workflow still runs. Ordinary Classic pushes keep the original release behavior.

Local Windows builds of both profiles passed on 2026-11-15, including the wired-profile check and all nine complete-image component bounds/CRC32 checks. The build restored the original Classic configuration afterward. These pre-commit images record the dirty source inventory in acceptance.txt; the Actions artifacts identify their committed source. The GUI image SHA256 is f3d0fddcd0a66d5985bfb68740cb9c4827a2885bc56524ae40b80c2479f31dfe and the Headless image SHA256 is b5d7e36f78df5ec355bcc7f428b2d68207081ed2ae6a6b5ecf91c2055613ac17. Neither has been flashed. The full application Host suite and actual LVGL 9 Settings preview passed. The published SDK milestone also passed [its GitHub CI](https://github.com/boa-z/rt-carplay/actions/runs/43734899981).

## Single-screen fusion implementation plan

FULLSCREEN preserves the existing Main Display. FUSION will scale MPP video directly through DE into a bounded window while keeping an ARGB LVGL instrument layer active. LOCAL will keep speed, gear, battery and connection state visible without video. Initial telemetry must say SIMULATED and expose a real-provider boundary. This application fusion is independent of official Ultra Local UI.

The current video_layer_policy hides the entire UI after first video, and the launcher stops page refresh while video is active; these policies must become mode-aware. Video already retains MPP buffers for DE scanout and VSYNC. Keep hardware composition, preserve buffer ownership on failed updates/shutdown, and avoid full-frame software copies. Share the actual aspect-fit rectangle with touch mapping, reject bars, retain press-to-release ownership across region crossings and cancel contacts on mode/session changes. LVGL calls stay on its own thread. Disconnect, reconnect and stale video must leave local instruments running. Stage timing/FPS/CPU/memory counters must retain their measurement meaning; receiver processing time is not phone-to-glass latency.

## Board preparation and evidence

On 2026-11-15 the connected board responded on COM9 at 115200. aiburn service access and software entry via aicupg were verified; the service detected one upgrade USB device. This mode required a user power cycle to leave without flashing. No firmware was written or erased for this check. Enter it only when both image and backup are ready.

The existing firmware's Classic wired session was observed over a 65,093 ms video interval: 1,169 additional frames, zero drops/errors/resets and queue peak one. Native NCM RX/TX/input errors were zero. The user confirmed normal visual smoothness, touch and audible playback. An audio startup gap was logged. This is a bounded current-firmware observation; its loaded image hash/source revision was not established, and it does not validate these new sources, physical unplug/replug or Ultra.

Receiver seed, AirPlay records and USB trust files were backed up outside Git with restricted local permissions. Two independent /data reads matched SHA256: six files, 51,252 bytes. A temporary push/pull round trip also matched. The backup had no bt_settings file, so Bluetooth bond persistence is unverified. Modern Android platform-tools with ADB_LIBUSB=1 identified CherryADB; the old bundled ADB did not. Transfer completion output and file hashes were checked because the modern client remained alive after transfer and had to be terminated separately. No pairing bytes, phone identifiers or private manifest are included in this repository. Recheck file inventory and backup stability immediately before a future full flash, restore required identities afterward, then verify automatic reconnect.

## Acceptance checkpoint

| Level | Status | Evidence still required |
| --- | --- | --- |
| L0 | PASS for initial source research | This review distinguishes code, unexecuted reference tests and official product requirements; further protocol research continues. |
| L1 | PARTIAL | Display/OPACK/Vehicle State Host tests passed; live duplex transport, negotiated composition and UI Sync remain. |
| L2 | NOT_RUN | Implement and physically validate D213 FULLSCREEN/FUSION/LOCAL, input, reconnect and performance. |
| L3 | NOT_RUN | Real-iPhone Ultra feature negotiation. |
| L4 | NOT_RUN | Real-iPhone Vehicle State and UI Sync exchanges. |
| L5 | NOT_RUN | Optional independent Type 111 sink and reception. |

Official Ultra product acceptance remains NOT_RUN. Unknown requirements are not marked BLOCKED until a concrete protocol, authorization or hardware constraint is established. The existing Classic board check is separately scoped and does not promote any Ultra level.
