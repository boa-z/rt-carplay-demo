# D213 single-screen CarPlay fusion

[简体中文](single-screen-fusion.zh-CN.md)

The Ultra GUI development profile enables `RTCARPLAY_SINGLE_SCREEN_FUSION` on the D213 1024×600 panel. This is Classic Main Display video combined with local LVGL 9 instruments. It does not implement Apple's Ultra Local UI renderer or establish official Ultra interoperability. The portable SDK gains no board or LVGL dependency. The normal Classic and Headless profiles retain their existing display policy.

## Phone resolution and black bars

The dedicated Ultra GUI defconfig advertises a 768x600 Main Display while the physical framebuffer and LVGL remain 1024x600. Previously, a 1024x600 phone image fitted into the left 768x600 viewport became 768x450, leaving 75 pixels above and below. A phone stream rendered at the advertised 768x600 fills that viewport without stretching or cropping; its actual resolution and touch behavior still require iPhone validation. Ordinary Classic and Headless defconfigs retain 1024x600.

A changed advertised size takes effect in a new CarPlay session after rebuilding/flashing. Mode buttons only change hardware composition. Switching this 768x600 session to FULLSCREEN centers the same image with 128-pixel bars on each side; it does not renegotiate 1024x600. Use a panel-sized profile/session for native full-panel output. Touch input remains physical 1024x600 at the launcher and maps to the latched logical phone dimensions.

## Modes and controls

| Mode | Presentation | Input |
| --- | --- | --- |
| `FULLSCREEN` | Aspect-fit video over the full panel; the UI layer has global alpha zero after video is available. | Phone coordinates map from the successfully latched video rectangle; letterbox bars are excluded. |
| `FUSION` | Aspect-fit video in the left 768×600 viewport; opaque local instruments occupy the right 256×600 strip. | Gestures belong to either the phone or local UI from press until release. |
| `LOCAL` | Full-panel local instruments, including while disconnected or connection setup fails. Connected video can continue decoding behind the opaque UI. | Local UI only. |

The default requested mode is FUSION. Until a video update and VSYNC succeed, the effective mode is LOCAL. Disconnection or decoder reset returns presentation to LOCAL; a later frame restores the requested mode. The dashboard offers mode and connection-page buttons. The existing launcher has a Vehicle instruments button. Use FinSH to return from FULLSCREEN:

```text
carplay_display status
carplay_display fullscreen
carplay_display fusion
carplay_display local
carplay_display simulate
```

The status command reports requested/effective modes, requested/latched layout generations, video presence, geometry, logical input size, display error and vehicle source. It never prints pairing material. No Type 111, HEVC, synthetic display catalogue or UI Sync capability is advertised by enabling this demo.

## Hardware composition and ownership

MPP continues to decode H.264 to its owned frame buffers. DE scales and positions the video layer. The GUI owner crops the existing opaque UI framebuffer to the right strip in FUSION and uses pixel alpha so the empty part of DE's full-panel UI surface remains transparent. Global alpha 255 made that area opaque on D213 and hid the video despite valid decode/layer counters. LOCAL restores the original geometry/alpha; FULLSCREEN uses global alpha zero. It preserves the current framebuffer physical address across LVGL buffer swaps. There is no added per-frame video software copy. This path requires the existing unrotated, uncropped 1024×600 UI layer; a geometry mismatch requests LOCAL and records an error.

Only a successful video layer update and VSYNC publish a new input rectangle. Static phone images are re-presented on mode changes without counting them as newly received frames. Failed swaps retain both potentially scanned frames. Shutdown/reset must disable the video layer and confirm VSYNC before returning frames or destroying MPP; failed fences retain ownership for retry. The UI policy attempts rollback on a layer/alpha/VSYNC transition failure and retries LOCAL from the GUI timer. Persistent display ioctl failure still requires diagnosis on the board; software cannot guarantee visible instruments after hardware failure.

Touch mapping uses the latched rectangle and negotiated logical dimensions. It rejects bars and a pending layout, prevents a second finger from stealing the first contact, releases a phone gesture at its last valid coordinates when it leaves the video, and cancels contacts on mode/session/geometry changes. A local gesture stays local across region boundaries. Periodic cancellation handles stationary fingers too. Bounded state copies use short single-core interrupt masking; no LVGL, device ioctl, logging or external callback runs inside that span.

## Vehicle provider and measurements

Speed, gear and state of charge initially show **SIMULATED DATA**. The ramp is a demo, not vehicle evidence. The application API `carplay_fusion_set_vehicle()` copies a validated `carplay_vehicle_sample_t` after initialization; set `available=false` to show unknown values. `carplay_fusion_use_simulated()` explicitly restores simulation. No CAN mapping or real vehicle timeout is guessed. Providers own their sampling/validity policy and should publish unavailability when their data is invalid. This API is independent of the portable Vehicle State Protocol model.

The dashboard updates every 250 ms, independently of connection-manager availability. It reports newly latched video FPS, drops, average queue/decode/present wall time, CPU load when the existing CPU usage package is enabled, and used heap when supported. Unavailable CPU/heap values show `n/a`. Re-presenting an old frame does not increase FPS. The existing video diagnostic shell retains detailed counters. These are receiver stage observations, not phone-to-glass latency; measuring end-to-end latency still needs a synchronized stimulus and external observation. A static video image alone is not treated as a failed connection, and local data keeps updating.

## Validation and remaining board work

`make -C application/rt-thread/rtcarplay check` includes actual coordinator, DE UI-policy and video-sink Host tests under ASan/UBSan. Coverage includes layout generations, coordinate boundaries, pointer ownership/cancellation, copied provider values, crop/alpha rollback, framebuffer address preservation, static-image mode changes, failure fences and repeat lifecycle regressions. `tests/launcher_preview` builds `launcher_preview` and `fusion_preview` against the actual LVGL 9 software renderer; the latter checks widget bounds, labels, mode buttons, provider unavailability and disconnect rendering. Its empty left viewport is not a captured phone image. The dedicated Ultra workflow runs both previews and uploads their PPM screenshots with Host logs.

Native Windows GUI and Headless builds passed on 2026-11-15, with nine component bounds/CRC checks per complete image. The full application Host suite also checks Classic behavior. Its audio priority test retries only the documented nonblocking lock-busy result before asserting the priority result; production audio behavior is unchanged.

L2 remains **NOT_RUN** until the exact built image is flashed and physical composition/input is observed. Before flashing, re-read and verify private pairing/identity backups; restore them afterward and check automatic connection. Record source pins, clean/dirty inventory, image SHA256 and raw board logs. Physically test FULLSCREEN/FUSION/LOCAL, phone and local touch boundaries, unplug/reconnect, decoding failures, persistent instruments, framebuffer swaps and resource/performance behavior. Host success does not replace these checks. L3–L5 and complete official Ultra acceptance remain separate and NOT_RUN.

The [first board flash checkpoint](evidence/d213-fusion-first-flash-20261115.md) records the initial connection failure. The [subsequent flash and alpha experiment](evidence/d213-vehicle-controls-flash-20261115.md) records real video, Vehicle State negotiation and the black-region failure: the user confirmed FULLSCREEN worked, and changing FUSION to pixel alpha restored the left video. Full L2 acceptance is still incomplete; the runtime workaround must also be verified in a newly built image, including right instruments, touch, reconnect and fault recovery. Current per-capability status is in that checkpoint; historical NOT_RUN results above are not new-image acceptance.
