[English](v2-verification.md) | [简体中文](zh-CN/v2-verification.md)

# Launcher V2 verification

## Delivered behavior

The completed [V2.1 architecture baseline](refactoring-v21-verification.md) remains in place. The application now starts USB and wireless discovery automatically, arbitrates one Receiver/media owner, retries failed startup and transport attempts, and restores discovery after disconnect. USB enumeration remains armed during wireless operation; shared protocol/media resources permit one active session. An existing wireless pairing or session is protected from USB takeover. Retry cooldown is bounded at 5/10/20/40/60 seconds. See [connection manager](connection-manager.md) for ownership and recovery details.

The actual 1024×600 LVGL 9 [Launcher V2](launcher-v2.md) provides connection progress, Bluetooth confirmation, saved-device management, wireless settings, microphone mute, existing audio tests and diagnostics. Settings are RAM-only; trust/bond storage remains persistent. Video readiness requires successful layer submission and vsync rather than decoded-frame count alone. Disconnect restores Launcher ownership. Audio negotiation, codecs, jitter, Mixer/Direct, encryption and AEC algorithms remain unchanged; the separate audio handoff still applies.

## Exact verified source and artifacts

| Component | Revision |
| --- | --- |
| Application code | `bb44b499eb1591266ec1fd6c6b8a3803317eed37` |
| Portable SDK | `57936326c311850e006310e94d2aa9d711ba6a1e` |
| Independent IPHETH | `2c87b69fda0d2dbeeb9df5180dd10157791d7371` |

[Application CI 43649107384](https://github.com/boa-z/luban-lite-carplay/actions/runs/43649107384) passed all three jobs. [Independent SDK CI 43648984405](https://github.com/boa-z/rt-carplay/actions/runs/43648984405) passed. Subsequent README/report changes are documentation-only; the images below identify the tested code revision explicitly.

| Image | Bytes | SHA256 |
| --- | ---: | --- |
| GUI | 4,798,976 | `b5bf06acdda91d0e39509ddfe6bcbd1670d7b0f5db90b657ef2fc2087200e958` |
| Headless | 4,004,352 | `486955094da6d5bce8fc395a99bcdac63f9d6fa0c6ddcc7ad5b3753d1d037a41` |

Both downloaded artifact sets passed exact source/submodule identity checks, all five SHA256 manifest entries, and nine IMG component bounds/CRC32 checks. Local copies are in `output/ci-verified-43649107384/`. Each image contains the same-tree baremetal Bootloader. These checks establish package integrity, not successful flashing or physical operation.

## Executed verification

| Check | Result and scope |
| --- | --- |
| Application Host suite | PASS; actual composition, media, pairing, USB and owner sources under ASan/UBSan with modeled device/scheduling boundaries |
| Automatic owner | PASS in GUI and Headless builds; contention, retries, radio/USB/Receiver failures, detach, session loss, first-frame timeout, bounded cooldown and clock wrap |
| USB initialization | PASS; actual class binding before manager subscription is retained and discovered |
| Video readiness | PASS; nonzero decoded count with failed layer/vsync does not report presented video |
| Actual LVGL rendering | PASS; seven previews, visible bounds and UI events for settings, devices, audio, Bluetooth and manager startup retry |
| GUI/Headless firmware | PASS; cross compilation, baremetal Bootloader build and complete IMG packaging |
| SDK baseline | PASS; 55/55 CTest cases and bilingual documentation gates recorded in the V2.1 report |
| Independent IPHETH | PASS; 3/3 external CherryUSB/EP0 tests and bilingual documentation gates recorded in the V2.1 report |
| Physical board acceptance | NOT_RUN |

The final local application and render logs are `output/ci-logs/v2-final-host-check.log`, `output/ci-logs/v2-startup-retry-check.log` and `output/ci-logs/v2-render-check.log`. Preview connection/device data is modeled; the rendered LVGL and application UI code are real. Preview success does not establish Bluetooth, panel or audio performance.

## Repository and documentation closure

Development and publication use the existing application branch `carplay-oss` and SDK branch `main`, with author/committer `boa-z <boa-z@outlook.com>` and active boa-z credentials. No PR was created. The SDK repository was backed up and recreated under the owner's explicit authorization to remove old PR records. Its new repository ID is 1458244070 and its all-state PR count was verified as zero. The new root differs from the prior verified SDK source only in README evidence/history notes; implementation and tests are preserved. The original shared checkout was not modified.

The verified offline history, PR metadata, CI logs, restoration check and source map are in `C:/Project/Carplay/backups/20261114-v2-final/`. Older SDK Gitlinks and CI links may require this backup. The parent repository retains upstream history. Current application README, connection manager, Launcher and verification documents have full English/Chinese counterparts; historical build notes explicitly defer to current documentation. Mechanical heading/code/literal/link checks supplement manual semantic review.

## Remaining board checks

No Windows serial port was enumerated and no active `aiburnconfig.json` was available; no flash or serial session was attempted. The previously reported bootable image does not validate this V2 image. Physical cold boot, both connection paths, simultaneous arrival, reconnect, long disconnect/recovery, first displayed frame, touch handoff, Bluetooth confirmation, stored-device reset, audio output and microphone remain NOT_RUN. Run those checks against the exact image/hash above and retain raw logs. The earlier connected-but-undisplayed report is addressed by presentation readiness and display ownership changes, but physical resolution is not yet established.
