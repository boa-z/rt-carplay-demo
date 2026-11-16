# D213 native-resolution display settings

[简体中文](d213-native-display-settings-20261115.zh-CN.md)

The user requested true phone-rendered 1024x600 full screen, a launcher setting and an investigation of compressed-looking fusion video. This revision recreates the phone session when changing between native 1024x600 and fusion 768x600; it does not merely enlarge the existing 768x600 image. The physical panel and LVGL remain 1024x600. No board, serial, ADB, reset or flash operation was attempted after the user assigned manual testing to themselves.

## Withdrawn revision and regression

The user subsequently reported repeated Bluetooth restarts with the e17d8fa2 image. This revision is withdrawn; the artifacts below identify the affected build, not a recommended download. Source inspection found that the fusion connection manager passed `status.wireless` into `carplay_app_autostart_display(bool wired, ...)`, reversing transport selection. The correction passes `!status.wireless`. The existing owner test mock repeated the same polarity mistake and has been corrected. No additional tests were added for this correction. Green Host/CI results for e17d8fa2 missed this defect. The source defect is confirmed; board logs were not collected, so complete causality and recovery remain pending manual verification.

## Affected artifacts

| Item | Evidence |
| --- | --- |
| Integration image revision | `e17d8fa2d6ba00d130643f9d80c8e7988e06f6f6` |
| SDK image revision | `06f90ad4846865746e297bf8178c71301f8c5c4d` |
| Native GUI build | `.ci/build-ultra-demo.sh gui`, Windows, exit 0 |
| Native GUI SHA256 | `4a7add9e171cd51779fdae82b1290599802394502af0335558127367bf427ce2` |
| Native artifact verification | Exact source pins and clean inventories, all six SHA256 entries, nine component bounds/CRC32 checks and config/ELF native-NCM/Vehicle checks passed. Physical config is 1024x600, default phone width 768 and estimated full-panel physical dimensions 200x120 mm. |
| Integration CI | [43791425762](https://github.com/boa-z/luban-lite-carplay/actions/runs/43791425762): Host, GUI and Headless passed. |
| SDK CI | [43789744217](https://github.com/boa-z/rt-carplay/actions/runs/43789744217): Host sanitizer and bilingual documentation passed. |
| Publication | Code and documentation pushed on existing branches; rolling Preview Release skipped. |

Native artifacts are preserved at `C:/Project/Carplay/rt-carplay-artifacts/ultra-research/native-e17d8fa2/gui/`. Build/Host logs in the research directory are `native-fullscreen-native-gui.log`, `native-fullscreen-integration-host.log`, `native-fullscreen-owner-host.log`, `native-fullscreen-preview.log` and `vehicle-diagnostics-sdk-host.log`. A PNG from actual LVGL rendering is preserved as `native-e17d8fa2/display-settings.png`; its phone/connection values are modeled, not device evidence.

## Behavior and verification

Connections > Settings > Display mode offers Full screen 1024 x 600, Video + meters 768 x 600 and Local instruments. Settings remains accessible from the live connection page in the fusion profile. The instrument full-screen button routes to the same owner request. USB/iAP2 borrowers close before the old Receiver; only afterward are Receiver/HID/sink and `/info` rebuilt. Requests coalesce, LOCAL retains phone geometry, same-size requests do not cause a geometry restart, and device/audio tools can refuse a request. Local instruments cover session retirement. Settings reset at reboot. Existing composition-only diagnostic commands retain their older semantics; use `carplay_connection display fullscreen|fusion|local` for owner-controlled switching.

Full application `make -j8 check` passed. Actual-owner fixtures cover 32 repeated USB/wireless cycles, width transitions 768->1024->768, close-before-start assertions, local preservation, blocked tools and pending-request cancellation. The actual app plist test independently reads widthPixels/heightPixels/widthPhysical/heightPhysical for both dimensions. The additional LVGL executable checks live Settings access, all three button requests, back navigation and bounds; Classic launcher and fusion previews also pass. No Host result proves a real phone accepted or rendered the new size.

Physical metadata previously stayed at estimated 200x120 mm after changing pixels to 768x600. The application now scales the declared physical viewport with the pixel viewport: defaults produce 150x120 mm for fusion and 200x120 mm for full screen. New Kconfig settings accept measured full-panel active dimensions. The defaults remain uncalibrated estimates. Whether this improves the reported compressed appearance is NOT_RUN; do not infer it from code or layout tests.

The SDK adds method attempt/serialized-response counters and the most recent method/lane/result. The FinSH application status includes them. A response can contain per-IID refusals; neither serialization nor transport enqueue proves phone receipt, successful actuation or real vehicle state. No payloads, transaction identifiers or pairing secrets are added to these diagnostic fields.

## Manual acceptance and hardware boundary

After receiving the preceding 8ac6f504 image, the user reported that black bars were gone and supplied a photo showing fusion video with right-side instruments, but described a compressed appearance. No independent version/hash readback accompanied the photo. This is bounded user visual evidence, not full touch/reconnect or new e17d8fa2 acceptance.

Before manual flashing, preserve pairing data and verify restoration/readback. Do not flash e17d8fa2. With a corrected replacement, choose Full screen 1024 x 600 and verify actual reconnection, full-panel content and edge touch. Then unplug to reach local instruments/Connections if necessary, select Video + meters 768 x 600 and verify complete content, proportions, local/phone touch boundaries and reconnect. `carplay_connection status` shows selected configuration; `carplay_display status` must independently show the actual latched logical/video geometry. The e17d8fa2 user-reported runtime regression is FAIL. Replacement-image phone acceptance is NOT_RUN.

Apple's [public architecture presentation](https://developer.apple.com/videos/play/wwdc2024/10111/) states that Local UI uses an OpenGL-based renderer, a dedicated compositor uses OpenGL, and HEVC streaming support is essential. The current D213 integration provides no implemented/verified OpenGL backend, official asset/presentation renderer or HEVC media path. GE2D/DE/LVGL fusion cannot stand in for them. A suitable graphics/codec platform and the required software, assets, access and qualification are needed for the complete official Ultra objective. This is an explicit current-platform capability gap; protocol/Host work remains useful and portable.

L0 remains PASS, full-scope L1 remains PARTIAL, earlier L2-L4 remain PARTIAL for their bounded evidence and real-phone L5 remains NOT_RUN. This revision does not enable UI Sync, HEVC or Type 111 on D213 and establishes no complete official Ultra acceptance.
