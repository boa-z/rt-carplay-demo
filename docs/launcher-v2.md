[English](launcher-v2.md) | [简体中文](zh-CN/launcher-v2.md)

# Launcher V2

## Application scope

Launcher V2 follows the [V2.1 software verification baseline](refactoring-v21-verification.md). It implements a 1024×600 LVGL 9 dashboard over the application [automatic connection manager](connection-manager.md). No new SDK, GUI dependency in the SDK, protocol framework, driver copy or development branch is introduced. USB/wireless policy and device ownership remain in the host application. Audio negotiation and processing stay frozen.

## Usable pages

| Page | Behavior |
| --- | --- |
| Home | Automatic USB/wireless discovery, connection steps, actual discovery status, PIN/SSID, uptime and retry |
| Bluetooth pairing | Current numeric comparison, consent or displayed passkey; matching-request confirmation and cancellation |
| Devices | Saved wired phone identifiers and CarPlay trust group, refresh, confirmed deletion and explicit all-Bluetooth-phones reset |
| Settings | Idle-only wireless discovery on/off with USB always enabled, actual microphone mute when built, panel resolution and system information |
| Audio | Existing tone/silence/AAC, rate/channel, gain/mute, start/stop, microphone level/loopback and input meters; controls run on the owner |
| Diagnostics | Independent transport/authentication/session/media/input states, network, radio/MFi, trust/storage errors, decoded counts, display handoff, recovery and retry wait |

The home page removes manual USB/wireless mode selection. Dark panels, consistent spacing, 32-pixel heading typography, high-contrast text, restrained accent colors and separate diagnostic pages replace the previous dense debug-first page. Panels use a touch-blocking dimmed backdrop. Device names are derived from validated stored identifiers; no user-friendly iPhone name is invented. CarPlay permissions and Bluetooth reset are group operations, explicitly confirmed. Identity material is protected. Settings are immediate, RAM-only and reset at reboot; unsupported microphone controls are disabled.

Video takeover occurs through the host display observer after a successfully latched frame. The launcher stops invalidating its hidden page while video is active. Disconnect retires the sink and restores the saved UI alpha. If decoded video exists but UI handoff fails, the visible launcher shows a display-check state and diagnostics. This does not replace physical panel verification. Headless uses the same connection owner without compiling LVGL.

## Reproduce the LVGL preview

The preview compiles the actual launcher and the repository's actual LVGL 9 implementation. It renders into a 1024×600 memory display and emits seven PPM images. Tests check visible page bounds and real LVGL events for wireless/microphone settings, saved-device confirmation, audio Start and Bluetooth confirmation. Transport/media/status inputs are explicitly modeled for rendering and request routing; these images are not board screenshots or protocol acceptance evidence.

```sh
cmake -S application/rt-thread/rtcarplay/tests/launcher_preview -B build/launcher-preview -DCMAKE_BUILD_TYPE=Debug
cmake --build build/launcher-preview -j8
mkdir -p output/launcher-v2-preview
build/launcher-preview/launcher_preview output/launcher-v2-preview
```

The firmware workflow's Host job runs these commands after the complete ASan/UBSan application suite and uploads the previews separately. GUI and Headless firmware jobs build matching Bootloaders and complete IMG artifacts. See [CI](ci.md) for dependencies and source/image identity. Rendering does not open a physical display, USB controller, radio, speaker or microphone.

## Hardware acceptance checklist

| Scenario | Status |
| --- | --- |
| Phone attached before power-on; automatic wired discovery | NOT_RUN |
| Wireless discovery at boot; new pairing and saved-bond reconnection | NOT_RUN |
| USB arrival during wireless pairing/handoff/live; no session theft | NOT_RUN |
| Wired and wireless unplug/disconnect/reconnect, repeated cycles | NOT_RUN |
| First visible CarPlay frame, UI alpha handoff and Launcher restoration | NOT_RUN |
| Actual panel touch, multi-touch CarPlay ownership and modal hit testing | NOT_RUN |
| Trust/Bond deletion, persistence after reboot and storage failure | NOT_RUN |
| sound0 playback, dmic0 levels/loopback/mute and long-run stability | NOT_RUN |

Record the exact source revision, image SHA256 and raw logs when testing. The earlier 38217874 image boot report and failed display report do not validate V2. Audio routing/zero-stream issues remain the separate SDK audio handoff. No PR is part of this delivery.
