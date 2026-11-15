[English](ultra-development.md) | [简体中文](ultra-development.zh-CN.md)

# CarPlay Ultra development and D213 fusion

The final product objective is complete official CarPlay Ultra. This development profile is an intermediate protocol and integration bench. Classic video plus LVGL instruments, Ultra-related negotiation and the official product have separate acceptance criteria. No appearance, popup, Host simulation or second video stream proves complete Ultra support.

## Sources and implementation baseline

Development uses boa-z/luban-lite-carplay on the existing carplay-oss branch; luban-lite-jc-carplay-oss is abandoned. The implementation baseline is integration 7be401243ff9b64eb94a64cfaa082cb6dc398c27 and SDK d0bc5190807279c62d08f78c3c428f3ca8704540. The first SDK foundation milestone is 183fd91167b9d2ca20e1b5f53882d3a6f1bf1ce9. Existing native NCM and short bulk-IN fixes are retained. The parent gitlink identifies the selected SDK revision.

Current application milestone: dedicated Ultra GUI/Headless configurations now opt into the [SIMULATED engine Vehicle State protocol bench](vehicle-protocol-demo.md). Ordinary Classic configurations remain unchanged. Earlier sections below describe their respective historical milestones; statements that D213 attaches no Vehicle processor predate this explicit experimental integration. UI Sync and its renderer/asset/presentation capabilities remain disabled on D213.

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

On 2026-11-15, cargo test --locked -p livi-cp --lib passed all 179 original LIVI tests on Ubuntu 24.04/WSL, Rust/Cargo 1.100.0 and OpenSSL 3.0.13. That includes 15 info tests, 27 Vehicle State tests, one broad UI Sync test and four iAP tunnel tests, covering metadata, configuration/read/register/write/notifications, reset/version/layout/ACK/focus/session/reopen and duplex framing. [The original selected test output](../../../../packages/third-party/rt-carplay/docs/evidence/livi-cp-host-20261115.txt) records the exact reference revision and environment. Source and Cargo.lock remained unchanged. Windows could not compile livi-base's Unix file-permission API, so no reference tests ran there. Later separate commands passed six livi-core theme_catalog tests and one livi-ui-model dashboard layout test; see the [additional source review](../../../../packages/third-party/rt-carplay/docs/livi-theme-review.md) and [original output](../../../../packages/third-party/rt-carplay/docs/evidence/livi-theme-host-20261115.txt). Unknown vendor layouts use a generic dashboard fallback in that model; metadata selection is not complete official theme rendering. These are reference Host results, not iPhone or board evidence.

## SDK milestone and remaining protocol work

The [portable foundation](../../../../packages/third-party/rt-carplay/docs/ultra-foundation.md) provides optional Main Display profile metadata, aspect-fit/touch geometry, bounded OPACK and a Vehicle State message processor. Host-supplied IDs, ownership, mutability, format/range validation and callbacks avoid inventing a vehicle catalogue. The next SDK milestone f67b10da52a0d344d90d9e31624a7d0c2c129c19 adds an internal authenticated duplex DataStream transport with complete-message assembly, sync-token replies, bounded copied queues, partial-write continuation, peer binding and terminal cleanup. Make now runs 58 test programs plus legacy E2E, and CMake passes 59 cases, with sanitizer, public-header, boundary and bilingual-document checks. Its [SDK CI passed](https://github.com/boa-z/rt-carplay/actions/runs/43737988065).

Both Windows GUI and Headless profiles were rebuilt with SDK f67b10d; compilation, wired-profile validation and all nine complete-image component bounds/CRC32 checks passed. The original Classic configuration and pack inputs were restored. No firmware was flashed for this milestone.

SDK milestone 83984a1165e1550d813d9edce6c0458bcc1c271c adds public Receiver transport registration/send/status APIs and Type 130 routing for Vehicle State normal/high and UI Sync UUIDs. Explicit host registration, the initial phone feature offer, completed Pair-Verify and encrypted control are required. Concurrent channels have independent ports, IDs, directional keys, queues and lifecycle. Failed listener attempts consume their salt; retired salts cannot be reused across these streams, screen, audio or iAP in the same session. Selective/all-Type-130 teardown and stale readiness are handled without retiring other media. The portable poll capacity is 24 for the optional endpoint set. See the SDK foundation document for bounds and callback ownership.

The independent encrypted Host controller passed three-channel TCP exchanges through both public and compatibility Receiver configurations, including offer/authentication gates, duplicate SETUP, cross-kind/media salt conflicts, failed-listener retirement, fresh-seed reopening, bad-tag isolation and cleanup of never-connected listeners on reconnect. SDK Make, application Host regressions and 59/59 CTest cases passed, as did Windows GUI/Headless compilation, wired-profile checks and all nine complete-image bounds/CRC32 checks. These are synthetic transport payloads, not real Vehicle State/UI Sync application or iPhone evidence.

The SDK routing milestone passed [its CI](https://github.com/boa-z/rt-carplay/actions/runs/43740826579). Integration 3a9e363d35040d9720df8d899028f78dc9c514c5 passed [Host and both firmware jobs](https://github.com/boa-z/luban-lite-carplay/actions/runs/43740941965). Downloaded GUI/Headless artifacts passed every SHA256 entry, source identity/clean-inventory check and all nine complete-image component checks; the Preview Release job was skipped.

The D213 profiles still register no new application protocol and use the existing receive-only iAP path. The SDK supports explicit Vehicle State and UI Sync processor attachment to Receiver, but does not compose their /info declarations. The next protocol step is capability declarations gated by complete handlers and actual host inventory. HEVC, displayPanels, altScreen and unsupported vehicle/UI features are not added by the development flag. SDK code stays independent of LVGL, MPP, DE and D213.

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

Local Windows builds of both profiles passed on 2026-11-15, including the wired-profile check and all nine complete-image component bounds/CRC32 checks. The build restored the original Classic configuration afterward. The first profile milestone's pre-commit images used SDK 183fd91 and record the dirty source inventory in acceptance.txt: GUI SHA256 f3d0fddcd0a66d5985bfb68740cb9c4827a2885bc56524ae40b80c2479f31dfe and Headless SHA256 b5d7e36f78df5ec355bcc7f428b2d68207081ed2ae6a6b5ecf91c2055613ac17. Neither has been flashed. The full application Host suite and actual LVGL 9 Settings preview passed. The published first SDK milestone also passed [its GitHub CI](https://github.com/boa-z/rt-carplay/actions/runs/43734899981).

The committed profile milestone 096de7eb242aba84ab868fd6b1e0f127ddd5483b passed [all dedicated CI jobs](https://github.com/boa-z/luban-lite-carplay/actions/runs/43736939136): Host, GUI and Headless. All three artifacts were downloaded; every SHA256 entry, clean source inventory and SDK/integration revision was verified, and both downloaded images passed all nine component bounds/CRC32 checks. The rolling Preview Release job was skipped. These checks establish traceable development artifacts, not board or phone acceptance.

## Single-screen fusion implementation

The Ultra GUI profile now implements FULLSCREEN/FUSION/LOCAL using DE video positioning and UI-layer crop, with explicit simulated instruments, a copied vehicle-provider API, shared latched touch geometry, lifecycle-safe scanout ownership and receiver-stage metrics. See [D213 single-screen fusion](single-screen-fusion.md) for controls, constraints, Host coverage and the physical acceptance checklist. Native GUI/Headless builds and actual LVGL Host previews passed on 2026-11-15; L2 remains NOT_RUN pending exact-image board validation. This application fusion is independent of official Ultra Local UI.

## Board preparation and evidence

On 2026-11-15 the connected board responded on COM9 at 115200. aiburn service access and software entry via aicupg were verified; the service detected one upgrade USB device. This mode required a user power cycle to leave without flashing. No firmware was written or erased for this check. Enter it only when both image and backup are ready.

The existing firmware's Classic wired session was observed over a 65,093 ms video interval: 1,169 additional frames, zero drops/errors/resets and queue peak one. Native NCM RX/TX/input errors were zero. The user confirmed normal visual smoothness, touch and audible playback. An audio startup gap was logged. This is a bounded current-firmware observation; its loaded image hash/source revision was not established, and it does not validate these new sources, physical unplug/replug or Ultra.

Receiver seed, AirPlay records and USB trust files were backed up outside Git with restricted local permissions. Two independent /data reads matched SHA256: six files, 51,252 bytes. A temporary push/pull round trip also matched. The backup had no bt_settings file, so Bluetooth bond persistence is unverified. Modern Android platform-tools with ADB_LIBUSB=1 identified CherryADB; the old bundled ADB did not. Transfer completion output and file hashes were checked because the modern client remained alive after transfer and had to be terminated separately. No pairing bytes, phone identifiers or private manifest are included in this repository. Recheck file inventory and backup stability immediately before a future full flash, restore required identities afterward, then verify automatic reconnect.

## Acceptance checkpoint

| Level | Status | Evidence still required |
| --- | --- | --- |
| L0 | PASS for initial source research | This review distinguishes code, unexecuted reference tests and official product requirements; further protocol research continues. |
| L1 | PARTIAL | Display/OPACK/Vehicle State/registered Type 130 transport Host tests passed; Vehicle State/UI Sync Receiver composition has encrypted Host coverage; gated capability declarations remain. |
| L2 | NOT_RUN | Physically validate the implemented D213 FULLSCREEN/FUSION/LOCAL, input, reconnect and performance. |
| L3 | NOT_RUN | Real-iPhone Ultra feature negotiation. |
| L4 | NOT_RUN | Real-iPhone Vehicle State and UI Sync exchanges. |
| L5 | NOT_RUN | Optional independent Type 111 sink and reception. |

Official Ultra product acceptance remains NOT_RUN. Unknown requirements are not marked BLOCKED until a concrete protocol, authorization or hardware constraint is established. The existing Classic board check is separately scoped and does not promote any Ultra level.

## Vehicle State application milestone

The SDK now optionally attaches a real Vehicle State processor to Receiver configuration, registering both authenticated lanes and managing response/notification retry, selective channel replacement and subscription reset at the authentication boundary. The encrypted simulated-controller scenario verifies config/read/write/register and HIGH notifications alongside Classic media; a raw UI backend can coexist. Dedicated fault tests cover queue exhaustion and rejection before an additional write. Delivery status exposes pending bytes, retries and abandoned buffers. This is Host application-protocol evidence, not L3/L4 or official Ultra acceptance. The D213 application still attaches no vehicle processor and advertises no new Vehicle State/UI Sync capabilities.

Validation on 2026-11-15 passed the full SDK Make suite, CMake 61/61 tests with ASan/UBSan, the complete integration Host suite and both native Windows GUI/Headless image builds with nine-component bounds/CRC checks. Integral step validation now uses exact integer modulo rather than a long-double libm function absent from this RISC-V toolchain. SDK commit 5c15c3699368585508d28dc284620dda988402f1 passed [its GitHub CI](https://github.com/boa-z/rt-carplay/actions/runs/43743788364). Local pre-commit artifacts retain dirty-source inventories; CI produces clean revision-specific artifacts. No new image has been flashed.

## UI Sync application milestone

The SDK now provides an optional UI Sync model and Receiver attachment: reset/version negotiation, nested plist layout/appearance/focus messages, phone-choice state, application ACK/noAck and retained output. Focus tokens come from the host; no reference constant is assumed. Independent Python plistlib fixtures and outgoing message checks complement the encrypted simulated-controller scenario. The latter exercises both actual application processors, queue exhaustion and rejection before applying a new layout, fresh-channel negotiation, bad-tag isolation, a surviving vehicle read and Classic media. Limits, sequence assumptions and remaining renderer requirements are recorded in the SDK UI Sync documentation. D213 still attaches neither processor and advertises no new UI/vehicle capabilities. L1 remains PARTIAL until gated capability composition; L2–L5 remain NOT_RUN.

Validation on 2026-11-15 passed the full SDK Make suite, CMake 64/64 with ASan/UBSan, the complete integration Host suite and native Windows GUI/Headless builds with all nine image-component bounds/CRC checks. The independent Python check parsed 28 outgoing UI Sync messages. This integration pins SDK a76a7fe03a592a4854f15b0fcd147dfd406cb702. Local images include their pre-commit dirty-source inventories; revision-specific CI images are the clean source artifacts. No new firmware was flashed.

## Vehicle State capability composition milestone

SDK e175a8f019d5c86b5e152397fb0a75d758d11668 adds an explicit Vehicle State negotiation opt-in. The /info builder can generate vehicleStateProtocolInfo from the attached model; Receiver validates the entire catalogue and current initial values at creation and before each new controller. Missing, duplicate, mismatched or stale metadata rejects the opt-in/session. A dynamic host rebuilds /info before reconnect when those values change. Initial SETUP enables vehicleStateProtocol only with that validated opt-in and a matching phone feature offer. Stream status distinguishes registered, offered and enabled; enabled is not phone execution or official acceptance.

Independent Python plistlib tests validate Unicode/surrogates/embedded NUL, finite float metadata, binary values and exact int64 values above 2^53, plus ten independently encoded or mutated metadata cases. Encrypted Host scenarios inspect enabledFeatures, reject a bad capability refresh, preserve the offer across repeated SETUP and omit Vehicle State for a new controller that does not offer it. SDK Make, the complete integration Host suite and CMake 65/65 with ASan/UBSan passed on 2026-11-15. The integration capability harness links the new portable Vehicle/OPACK dependencies explicitly.

D213 profiles still attach neither processor and keep negotiate_vehicle false. UI Sync declarations still require actual display/theme/rendering inventory; no synthetic displayPanels, HEVC or Type 111 is added. L0 remains the documented initial research PASS, L1 remains PARTIAL for the full requested scope, and L2-L5 remain NOT_RUN. No firmware was flashed during this milestone. GUI/Headless images from this commit's dedicated CI are build artifacts, not board or real-iPhone acceptance.

## DE scanout lifetime before fusion mode switching

The video sink now retains the previously displayed frame and at most one attempted replacement when a layer update or VSYNC fails. It retries that replacement before acquiring more decoded frames, including when no new output arrives. Only a confirmed update/VSYNC transfers ownership and returns the previous frame to MPP. Presentation timings include these retries. This remains the existing hardware-layer path and adds no per-frame image copy.

Decoder recovery and stop disable the DE video layer and wait for VSYNC before returning either scanout or destroying the decoder. The decoder worker leaves scanouts to the joined lifecycle owner. A failed disable or VSYNC makes stop return RTCP_EIO while retaining the decoder, frames and synchronization resources; the caller must retry stop before restart or observer replacement. The existing application shutdown owner already retries unsuccessful stop calls. Worker timeout continues to retain resources as before.

Actual-source Host tests model uncertain scanout ownership and assert that decoder destruction cannot occur while hardware may still read a frame. They cover failed replacement/retry, recovery failure, disable failure, stop fence failure, retained-resource retry, observer ordering, the existing packet/recovery behavior and repeated lifecycles. The full integration Host suite and native Windows Ultra GUI/Headless builds passed on 2026-11-15; both images passed nine component bounds/CRC checks. Real D213 fault injection and fusion display acceptance remain NOT_RUN. This prerequisite does not yet implement FULLSCREEN/FUSION/LOCAL switching or official Ultra rendering.

## Copied UI Sync inventory milestone

SDK 27de06565aadb460fb10520812b4ebf8c3521209 adds an optional copied host inventory: theme/panel/display identifiers, pixel/physical dimensions, supported layouts and their palettes/zone-component alternatives. Unknown local or phone choices are rejected before committing state or ACK, and inventory survives channel reset. The Receiver records the exact last_rejection separately from a later generic close error. This is a portable contract for future real host capability composition, not a renderer or an implicit uiSync/displayPanels declaration.

SDK Make, CMake 65/65 under ASan/UBSan and the complete integration Host suite passed. Independent Python fixtures/output decoding cover 35 UI Sync messages. Encrypted Receiver tests reject an authenticated unknown layout on only the UI lane, continue Vehicle State reads, retain the specific refusal and reopen for supported choices. D213 still binds neither UI Sync inventory nor the application processor and advertises no new UI capability. Its existing fusion profile is unchanged.

The additional unmodified LIVI reference runs passed six theme-catalog tests and one dashboard-layout test. They used synthetic JSON/ZIP/catalog data and local cache files; no Apple theme assets were fetched or copied. The reference extracts IDs/palettes/map zones and defaults unknown vendor layout IDs to a generic dashboard, which does not prove full official Local UI rendering. The bilingual SDK review records the precise source/test limits.

The previously flashed board image remains 1b96d3f8, not this SDK pin. A new COM9 observation still showed FUSION requested, LOCAL effective, video=0 and no display error; USB was attached in CarPlay configuration, while Lockdown connection still failed before video. Existing private restored-file checks remain separate from successful automatic reconnect. L1 stays PARTIAL, full L2 and L3-L5 stay NOT_RUN. No additional firmware flash was performed for this milestone.

The initial inventory CI stopped at the bilingual documentation gate: the new review had incorrect language navigation and unmatched source references. SDK 284683f55809718ed83713797ea95f29857f9568 corrects those documents; the complete local Make check, including the documentation gate, passed again. Integration now pins that correction. This does not change the board firmware or phone acceptance status.

SDK 3d10a1e1f3b141acdbcfb570ebea49ab10817507 adds independent embedded-NUL fixtures for all four phone layout/palette identifier fields. Existing parsing refuses the complete strings with EINVAL, preserving phone choices and sequence/ACK state without output. The full local SDK check passed, including 35 independently decoded UI Sync outputs. This is additional rejection coverage, not a protocol implementation change.

The [D213 codec/graphics review](d213-ultra-feasibility.md) records the actual MPP factory/build and GE interface evidence, the public architecture requirements and the remaining silicon/runtime questions. The current path has no HEVC decoder entry; this does not prove the silicon cannot support another vendor implementation.

## Inventory CI correction and verified artifacts

SDK 3d10a1e1f3b141acdbcfb570ebea49ab10817507 passed [both SDK CI jobs](https://github.com/boa-z/rt-carplay/actions/runs/43758862128). Integration 538a7a8fa8fcda1de1c4c3a91a8fd327dcce21cf passed [Host, GUI and Headless CI](https://github.com/boa-z/luban-lite-carplay/actions/runs/43758941951). These successful runs supersede the initial failed bilingual-document gate; the failure was not a sanitizer or wire-protocol failure.

All three integration artifacts were downloaded. Both firmware manifests matched the exact integration/SDK revisions above and recorded clean source inventories. All six SHA256 entries per firmware artifact passed; each complete image passed nine component bounds/CRC32 checks. GUI image SHA256: 3d055d00f507cb2ae290bf1a96652702577121b4889e761361a46c79d7b5e901. Headless image SHA256: 0b9bcdcfd56e413d7ce1aee8c8139f3605cef28170907c11707f5b2ade5a991f. Documentation-only follow-up commits do not change those artifact identities.

These downloaded images have not been flashed. The connected board remains on the previously recorded 1b96d3f8 image; no new automatic-reconnect, physical fusion/touch or real-iPhone Ultra result is claimed. L0 source research remains PASS, L1 remains PARTIAL for the full protocol scope, and L2-L5 remain NOT_RUN. The rolling Preview Release was skipped; no Release was published for these milestones.

## Experimental single-Main-Display UI Sync capability gate

SDK a25c0cb99553492bc6d65d5f84fddceb25cfebe6 adds [explicit experimental UI Sync capability negotiation](../../../../packages/third-party/rt-carplay/docs/ui-sync-capabilities.md). The /info builder accepts the attached model only with a copied inventory matching Main Display UUID and pixel/physical geometry. It emits empty uiSyncInfo and exactly one real displayPanels entry with the single Type 110 stream at origin zero. It adds no virtual instrument panel, display plugin, Type 111, altScreen or HEVC.

Receiver negotiate_ui_sync independently gates enabledFeatures on the attached processor, bound inventory, matching initial/refreshed metadata and the phone's offer. Default Classic response bytes are retained. Invalid refreshed UI metadata rejects a new controller even when Vehicle metadata remains valid. The immutable /info snapshot can expose explicitly supplied UI metadata before SETUP; LIVI's uiSyncInfo is conditional after the offer. Real-phone acceptance of this narrow experiment remains NOT_RUN. The host must supply its real asset/renderer/presentation contract and keep inventory immutable while attached; the metadata gate does not validate those implementations.

The complete SDK Make check passed; CMake with ASan/UBSan passed 66/66. Independent plistlib checks cover 65 metadata cases and all four Vehicle/UI SETUP feature combinations. The encrypted Receiver scenario verifies enabled features, UI/Vehicle exchanges, UI-only refreshed metadata refusal, reconnect/no-offer behavior and unchanged Classic media. The complete integration Host suite passed after adding the actual UI model/value-writer sources to its manually linked capability test. The test also verifies every existing D213 capability profile retains one Type 110 display and omits UI/HEVC/panel declarations.

D213 still attaches neither processor and sets neither negotiation opt-in. Its LVGL fusion dashboard does not implement the renderer/assets/presentation contract and remains an independent application demo. No firmware was flashed for this milestone. L0 remains PASS, full-scope L1 PARTIAL and L2-L5 NOT_RUN.

## Single-Main UI Sync CI and board observations

SDK a25c0cb99553492bc6d65d5f84fddceb25cfebe6 passed [sanitizer Host and bilingual documentation CI](https://github.com/boa-z/rt-carplay/actions/runs/43761277096). Integration 2808437f848c046a1f90798f72023cffb989f297 passed [Host, GUI and Headless CI](https://github.com/boa-z/luban-lite-carplay/actions/runs/43761359152). All three artifacts were downloaded. Both firmware acceptance manifests matched these exact revisions and recorded clean SDK/integration inventories; all six SHA256 entries per artifact and all nine complete-image component bounds/CRC32 checks passed.

GUI image SHA256: 3cc1343e289e5134fab37542400402ad98bef5f0759b4f774574a8a4ce5ee01d. Headless image SHA256: 7b1075016ad1ea0161546d2bd282ed99369781a27958987c63443f1ceb5a1b91. Neither image was flashed. The rolling Preview Release was skipped. The documentation-only follow-up does not change the source identities of these images.

Read-only COM9 queries through the project aiburn skill on 2026-11-15 returned requested=FUSION, effective=LOCAL, generation=3, latched=0, video=0, error=0, and a SIMULATED vehicle sample. The connection manager reported owner=wireless, stage=1, usb_ready=1, attached=1 and bt_ready=1. These are point-in-time coordinator/connection observations on the existing firmware, not visual composition, automatic-reconnect or continuous-uptime evidence. No reset, upgrade-mode entry or identity-file transfer was performed. Raw logs remain outside Git in the restricted private device directory; no phone identifiers or pairing material are included here. Physical fusion/input and real-iPhone Ultra acceptance remain NOT_RUN.

## Ordered UI Sync choices for host renderer handoff

SDK d6ab43aef99f39c31bcffaab317b0352cef35136 adds optional queue_phone_choices and copied peek/consume APIs. The sixteen-entry FIFO retains layout, palette and appearance order with source session/sequence and a local reset generation. A layout plus palette requires two free slots atomically. The default keeps existing snapshot-only behavior and allocates no choice-queue workspace. This local handoff adds no phone-facing feature, renderer, asset runtime or presentation clock.

Full queues refuse the current command before state/ACK commit, preserving earlier records. Successful protocol/channel resets count abandoned queued records and advance generation; hosts must invalidate their older copied work. Consumed choices mean only host acceptance, not a rendered or presented frame. The [SDK state/ownership document](../../../../packages/third-party/rt-carplay/docs/ui-sync.md) describes the lifetime, noAck, failure and reset contracts. It follows LIVI's distinct phone choices while adding explicit bounds and lifecycle handling.

Complete SDK Make, 66/66 CMake tests under ASan/UBSan and the complete integration Host suite passed locally. Tests cover copied input/output lifetime, ordering, noAck, stale/duplicate suppression, two-record refusal, ring wrap/retry, transactional failed RESET and abandonment. Encrypted Receiver tests consume actual choices, distinguish reopened generations, fill the host FIFO while transport drains, refuse the next command only on UI and continue Vehicle reads. D213 still attaches no UI processor and advertises no UI Sync capability; no firmware was flashed. L0 remains PASS, full-scope L1 PARTIAL and L2-L5 NOT_RUN.

## Ordered UI Sync handoff CI and image verification

SDK d6ab43aef99f39c31bcffaab317b0352cef35136 passed [sanitizer Host and bilingual documentation CI](https://github.com/boa-z/rt-carplay/actions/runs/43763712003). Integration 2f5f5895d019503c600361aa573bede2e1f6196b passed [Host, GUI and Headless CI](https://github.com/boa-z/luban-lite-carplay/actions/runs/43763767218). All three artifacts were downloaded. Both firmware acceptance manifests matched these exact revisions and recorded clean SDK/integration inventories. All six SHA256 entries per firmware artifact and all nine complete-image component bounds/CRC32 checks passed.

GUI image SHA256: 324226e2a572111d7943e743227960fecc10bc1097c4e64c2321999f29a31971. Headless image SHA256: 9d938362522e9cc57ae81431045d886cc22d65fdf33e4658ddc87c0653d40b47. Neither image was flashed. The rolling Preview Release was skipped; the documentation-only follow-up does not change these image identities. The queue is a host handoff foundation, not renderer or physical presentation evidence. L0 remains PASS, full-scope L1 PARTIAL and L2-L5 NOT_RUN.

## Application Vehicle State experiment

The dedicated Ultra profiles attach a car-owned three-characteristic combustion-engine simulator, declare the matching /info catalogue and opt into phone-offered vehicleStateProtocol. The application updates the model on the Receiver owner, refreshes current initial values before each controller and destroys the Receiver before releasing the model. carplay_status publishes copied simulation/protocol/delivery observations. Road speed, gear, battery SOC, UI Sync, display panels, HEVC and alternate video streams are not inferred from this engine model. See the [model, source mapping and board procedure](vehicle-protocol-demo.md).

The complete integration Host suite passed locally, including enabled/disabled actual application startup/stop, metadata refresh and processor lifetime checks, subscription/HIGH notification and refused-write tests, plus independent plistlib validation of the generated catalogue and missing unsupported declarations. Dedicated image CI checks the actual model/protocol symbols in both firmware ELFs and records the simulated source in acceptance manifests. This milestone has no new board or phone acceptance; L0 PASS, full-scope L1 PARTIAL and L2-L5 NOT_RUN remain unchanged.

## Vehicle State application CI and device preparation

Integration aa4b325ee1dd23ee39d1a5f2b3c2ad94a07660dc, with unchanged SDK d6ab43aef99f39c31bcffaab317b0352cef35136, passed [Host, GUI and Headless CI](https://github.com/boa-z/luban-lite-carplay/actions/runs/43765510252). All three artifacts were downloaded. Both firmware manifests match these exact revisions, record clean integration/SDK inventories, identify SIMULATED_ENGINE_RPM with PHONE_OFFER_REQUIRED and UI Sync disabled. All six SHA256 entries per firmware and all nine image component bounds/CRC32 checks passed. The downloaded ELF/config pairs also passed the wired-profile verifier locally, including actual simulation and protocol symbols.

GUI image SHA256: 0b4f74869c5a7d5beaf5ce8ba34d5cf40877d32376f217c1d8a68c47f3c5d269. Headless image SHA256: 3b2927be7ffb8d8d15822985d5e61392944c23823f651b3b84d5daf8ea5b1b1a. No Release was published; the rolling firmware workflow was skipped. The following documentation-only commit does not change these image identities.

Device preparation on 2026-11-15 found one ready ADB file-sync device. Two fresh independent /data reads contained six identity/pairing files totaling 51,252 bytes; all file SHA256 values matched each other and the earlier verified backup. The raw files, manifests and logs remain outside Git in the restricted private device directory. Completed transfer clients that remained running were terminated individually; the ADB server and serial monitor were not stopped. No Bluetooth settings file was present, so this does not establish bond persistence or automatic reconnection.

The aiburn serial command returned success but did not provide runtime output. A separate readlog returned no serial log available. Shell reachability was therefore not established at this checkpoint. No aicupg, flash, reset or restore was performed, and the simulation has not been run on D213. The board keeps the earlier documented firmware. L0 PASS, full-scope L1 PARTIAL and L2-L5 NOT_RUN remain unchanged; resume with a confirmed debug channel before entering upgrade mode and take another fresh backup if device state has changed.

## Independent Main/alternate video session foundation

SDK 8a0d71acb97bbc62426bc96ae13872f2baa2b95d separates Type 110 and experimental Type 111 session state through explicit paired internal video callbacks. Mixed SETUP, distinct ports, per-slot replacement/teardown, stale teardown filtering, shared connection-ID reservation and transaction rollback are covered. Default production Receiver/server composition still uses the existing Main listener; it does not attach the new callbacks, declare a second display or enable altScreen. The [SDK contract and remaining stages](../../../../packages/third-party/rt-carplay/docs/video-stream-session.md) distinguish this control-plane foundation from a second encrypted listener, media sink and L5 reception.

Local complete SDK Make, 67/67 CMake tests under ASan/UBSan and the complete integration Host suite passed. Python plistlib independently supplies mixed-stream fixtures and decodes distinct Main/alternate response ports, unchanged initial features and legacy omission of the unsupported alternate stream. Callback tests cover duplicate/colliding IDs, idempotency, failed/zero-port callbacks, audio and response-encoding failure, rollback and exactly-once cleanup. They are not encrypted Type 111 or real-phone evidence. D213's simulated Vehicle State profile and independent LVGL fusion behavior remain unchanged.

A follow-up device check found COM9 registered and serial writes accepted, but again no runtime output. The upgrade interface reported no enumerated upgrade device. These observations do not establish shell reachability or the cause of silence. No upgrade-mode entry, flash or reset was attempted. L0 PASS, full-scope L1 PARTIAL and L2-L5 NOT_RUN remain unchanged.

## Independent video session CI and artifact verification

SDK 8a0d71acb97bbc62426bc96ae13872f2baa2b95d passed [sanitizer Host and bilingual documentation CI](https://github.com/boa-z/rt-carplay/actions/runs/43768403492). Integration 5be76b8b9b322481d9d40bc1a6839c9f3da1ff7f passed [Host, GUI and Headless CI](https://github.com/boa-z/luban-lite-carplay/actions/runs/43768478380). All three artifacts were downloaded. Both firmware manifests matched these exact revisions and recorded clean SDK/integration inventories. Every firmware's six SHA256 entries and nine complete-image component bounds/CRC32 checks passed.

GUI image SHA256: fc31e995d7e2d793019031f535343c281297de16adaaa8e5379f259c92defee1. Headless image SHA256: 1fc2a4e97790d4486b6b872d13568a40b9e4cdffdd01cf7bf5d3a8690bf93e0f. The downloaded ELF/config pairs passed the wired/simulated-model verifier locally; manifests retain SIMULATED_ENGINE_RPM, PHONE_OFFER_REQUIRED and UI Sync disabled. The production server still does not attach the experimental alternate-video callback pair. Neither image was flashed; the rolling Preview Release was skipped. The documentation-only follow-up does not change these image identities or the unchanged L0 PASS, full-scope L1 PARTIAL and L2-L5 NOT_RUN acceptance.

## Experimental independent alternate video transport

SDK 344ce0c2a03dd545961bb11ccaa2d2805efa6954 adds an explicit public Receiver experimental_alt_video gate, independent Type 111 H.264 TCP listener, per-connection derived key/nonce epoch, separate media callback or intentional Null Sink and owner-thread status/statistics. Alternate socket/authentication failure retires only that endpoint and keeps the shared consumed-ID ledger; Main loss retains full controller cleanup. The default Classic path and dedicated D213 Ultra configurations keep the gate disabled. No second-display /info, altScreen, HEVC, second hardware decode or official renderer is advertised. The [SDK contract](../../../../packages/third-party/rt-carplay/docs/video-stream-session.md) describes the API and remaining capability gate.

Complete local SDK Make, 69/69 CMake tests with RTCP_SANITIZE enabled and the complete integration Host suite passed. New public Receiver scenarios use genuine Pair-Setup/Pair-Verify, encrypted RTSP and interleaved independently encrypted Main/alternate TCP frames. They cover authentication/initial-SETUP gates, distinct ports/sinks/counters, Null Sink, ID reuse refusal, stale teardown, listener failure, error-only readiness without blocking accept/recv, wrong-key/bad-tag/EOF isolation, fresh-ID recovery, Main-loss cleanup and stop/start reset, then continue Classic media/reconnect regression. The bilingual documentation checker passed with 31 pairs and one explicit exemption. These are synthetic Host transport results; L0 PASS, full-scope L1 PARTIAL and L2-L5 NOT_RUN remain unchanged. No new board flash or phone interaction occurred in this milestone.

## Alternate video transport CI and artifact verification

SDK 344ce0c2a03dd545961bb11ccaa2d2805efa6954 passed [sanitizer Host and bilingual documentation CI](https://github.com/boa-z/rt-carplay/actions/runs/43771017888). Integration b15c1c9c2acae83003f618f7c5eb573556518d48 passed [Host, GUI and Headless CI](https://github.com/boa-z/luban-lite-carplay/actions/runs/43771086125). All three artifacts were downloaded. Both firmware manifests matched these exact revisions and clean source inventories. Every firmware's six SHA256 entries and nine complete-image component bounds/CRC32 checks passed. Both downloaded ELF/config pairs passed the wired/simulated-model verifier locally. The downloaded Host log confirms both encrypted alternate-video scenarios ran successfully.

GUI image SHA256: cef23d88e9f745603f342320517df6b8ea0f23dde899c5b7d3c36f1cfd709ddc. Headless image SHA256: e9b8b1430161c72a8460742b7948230b2f891f6559d22f5697ed852040a0c8f1. Manifests retain SIMULATED_ENGINE_RPM, PHONE_OFFER_REQUIRED and UI Sync disabled; the application does not opt into experimental alternate video. Neither image was flashed. The rolling Preview Release was skipped. This documentation-only follow-up does not change image identities, L0 PASS, full-scope L1 PARTIAL or L2-L5 NOT_RUN.

## Optional Vehicle State service controls

SDK d0a15bbd4312df82dca208ef3cb2b519cc91c357 adds [typed service-level controls](../../../../packages/third-party/rt-carplay/docs/vehicle-controls.md) to the existing Vehicle State processor. Hosts explicitly supply command IID/type, existing service, request/response schemas and pure preparation/post-serialization commit callbacks. The model copies the catalogue and parameters, rejects malformed batches before preparation, returns reference -6735 for unknown IDs and invokes no execution callback until the complete response serializes. Response retention/retry uses the existing Receiver delivery path. No actual vehicle mapping, actuator, image package or control is inferred; the D213 simulated engine profile still registers none.

The complete local SDK Make check, 72/72 CMake tests with RTCP_SANITIZE enabled, complete integration Host suite and bilingual checker (32 pairs, one explicit exemption) passed. Independent Python OPACK/plistlib checks validate requests, responses, service metadata and mismatching/reordered catalogue rejection. The encrypted public Receiver scenario executes controls on Normal/HIGH, refuses a malformed later command without committing the earlier one, preserves HIGH reads, reopens with a fresh seed and continues Classic media/reconnect regression. Synthetic command execution is not real-iPhone or actuator evidence; L0 PASS, full-scope L1 PARTIAL and L2-L5 NOT_RUN remain unchanged.

A new read-only device check found COM9 registered. One aiburn command returned buffered runtime text, but no confirmed shell prompt or valid display-status result; subsequent correct carplay_display status queries produced no runtime output. The upgrade query found no USB upgrade device. These observations do not establish board health or why the shell is unavailable. Raw output was kept in the private device directory outside Git. No upgrade-mode entry, flash, reset, pairing-file modification or phone interaction was performed; board acceptance still requires a confirmed debug channel and a fresh verified pairing backup before flashing.
