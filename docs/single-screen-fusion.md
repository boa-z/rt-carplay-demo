[English](single-screen-fusion.md) | [简体中文](zh-CN/single-screen-fusion.md)

# D213 single-screen CarPlay fusion

The unified D213 image enables `RTCARPLAY_SINGLE_SCREEN_FUSION` on the 1024×600 panel. This is Classic Main Display video combined with local LVGL 9 instruments. It does not implement Apple's Ultra Local UI renderer or establish official Ultra interoperability. The portable SDK gains no board or LVGL dependency. A build without the LVGL launcher keeps the older panel-sized display policy.

## Native full-screen and fusion settings

Open **Settings → Display → CarPlay display** and select **Fullscreen** or **Fusion**. Fullscreen requests native 1024x600 video. Fusion requests 768x600 video beside the 256-pixel instrument strip. Both wired and wireless connections use the selected geometry. The shell commands below also provide a local instrument mode.

Full screen shows native 1024x600 CarPlay video. The local UI hardware layer is transparent, and every point inside the latched video rectangle belongs to the phone. Use the OEM tile or `carplay_projection pause` to return to the launcher during the session.

Geometry changes are asynchronous: the connection owner closes USB/iAP2 borrowers before stopping the old Receiver, recreates the Receiver/HID/sink and `/info` at the selected size, then allows reconnection on the same wired/wireless owner. Local instruments remain visible during retirement; LOCAL retains the chosen phone geometry. Repeated same-size requests do not restart the session, and pending requests coalesce. Device/audio tools can temporarily refuse a change. The applied display mode is saved in the application preferences and restored before the connection worker starts. LOCAL keeps the previous phone geometry during that boot; after reboot it uses the default fusion geometry while retaining LOCAL presentation. Real-iPhone reconnection and native output remain NOT_RUN for this revision.

**Settings → Language** changes local labels immediately. English is the first-boot default. Language, wireless discovery and microphone mute choices persist with display mode in `/data/carplay-ui.bin`. A versioned, checksummed record and a backup support recovery during replacement. Invalid or missing data uses defaults. Saving errors are logged and shown in Settings. Include this file in a private `/data` backup if preserving preferences across a full flash. See [font provenance and regeneration](../ui/fonts/README.md).

When a new connection reaches LIVE after video has been presented, the launcher closes its settings/read-only pages and automatically resumes the selected display mode. This runs once per successful connection, so opening settings deliberately during an existing fusion session remains possible. A failed/reconnecting session does not count as success.

The equivalent owner requests are `carplay_connection display fullscreen`, `carplay_connection display fusion` and `carplay_connection display local`. `carplay_connection status` shows the selected size and whether retirement is pending. `carplay_display status` reports the actual latched video/input geometry. The older `carplay_display fullscreen/fusion/local` commands remain composition-only diagnostics; they do not rebuild the session.

Physical size metadata now follows the viewport: `RTCARPLAY_PANEL_WIDTH_MM` and `RTCARPLAY_PANEL_HEIGHT_MM` describe the full LCD active area, and the application scales them into each phone viewport. Their defaults preserve the previous estimated 200x120 mm; a 768x600 fusion viewport therefore reports 150x120 mm, while native full screen reports 200x120 mm. These estimates are not measured panel calibration. Configure measured active width/height, excluding the bezel, for correct layout density and physical proportions. This fixes the earlier mismatch of a smaller pixel viewport with a full-panel physical width; actual improvement of the reported compressed appearance still needs a phone test.

## CarPlay frame rate

In **Settings → Display → CarPlay frame rate**, the default is 60 fps. A saved 30 fps selection stays in effect. Restart the receiver before connecting to apply the new `/info` `maxFPS`. Rebooting the board restarts the receiver. The D213 1024×600 panel timing is approximately 60 Hz. The iPhone chooses its actual video rate, and the video sink retains each scanout buffer through the display fence.

For a board comparison, use Fullscreen and the same active animation for at least ten seconds at each setting. Sample `carplay_status` and `carplay_video status` at both ends of the interval. Compare `offered_max_fps`, the incoming rate, `presented_fps`, queue drops and stage times. Repeat on wired and wireless connections. Only a sustained measured `presented_fps` near 60 establishes the target on the board.

## Phone resolution and black bars

The unified image advertises a 768x600 Main Display in FUSION while the physical framebuffer and LVGL remain 1024x600. Previously, a 1024x600 phone image fitted into the left 768x600 viewport became 768x450, leaving 75 pixels above and below. A phone stream rendered at the advertised 768x600 fills that viewport without stretching or cropping; its actual resolution and touch behavior still require iPhone validation. FULLSCREEN advertises the native 1024x600 again.

Changing build-time dimensions requires a new image/session. Runtime Settings now recreates the session at the requested native size as described above. A composition-only diagnostic change to FULLSCREEN within an existing 768x600 session centers that same image with 128-pixel side bars. Use Settings or `carplay_connection display fullscreen` for a new 1024x600 session. Touch remains physical 1024x600 at the launcher and maps to the latched logical phone dimensions.

## Modes and controls

| Mode | Presentation | Input |
| --- | --- | --- |
| `FULLSCREEN` | Native 1024x600 video with the local UI layer fully transparent and no settings overlay. | All phone coordinates map from the successfully latched video rectangle; no local corner is reserved. |
| `FUSION` | Aspect-fit video in the left 768×600 viewport; opaque local instruments occupy the right 256×600 strip. | Gestures belong to either the phone or local UI from press until release. |
| `LOCAL` | Full-panel local instruments, including while disconnected or connection setup fails. Connected video can continue decoding behind the opaque UI. | Local UI only. |

The first-boot requested mode is FUSION; later boots restore the saved mode. Before video starts the launcher is visible. Until a video update and VSYNC succeed, effective instrument presentation is LOCAL. Disconnection or decoder reset returns presentation to LOCAL; a later frame restores the requested mode. Dashboard Display and launcher Settings share one display page. The strip's "Dashboard" control (left arrow, bottom of the strip) selects the launcher dashboard and hands the panel back to the local pages while the session stays live; Connectivity's action then reads "Resume CarPlay" and restores the projection. FinSH commands remain available for diagnosis:

```text
carplay_display status
carplay_display fullscreen
carplay_display fusion
carplay_display local
carplay_display simulate
```

The status command reports requested/effective modes, requested/latched layout generations, video presence, geometry, logical input size, display error and vehicle source. It never prints pairing material. No Type 111, HEVC, synthetic display catalogue or UI Sync capability is advertised by enabling this demo.

## OEM tile and session-local switching

The launcher advertises an **rt-carplay** OEM tile over both USB and wireless. The embedded 180 × 180 PNG comes from [the demo logo SVG](../assets/rt-carplay-oem.svg). After editing the SVG, run `python tools/generate_oem_icon.py` from the application directory with Pillow installed, rebuild, and reconnect to negotiate the updated `/info`. Firmware builds use the generated header. Custom hosts can supply `carplay_app_config_t.oem_icons`, `oem_icon_count` and `oem_icon_label` before `carplay_app_start()`. Keep the assets available until stop because the app refreshes `/info` for each peer.

The CarPlay OEM tile asks the demo to show the local launcher. The demo switches the local display immediately and leaves the existing session available. Tap **Dashboard** on the instrument strip or run `carplay_projection pause` to explicitly request MainScreen for the host.

From the launcher, open **Connectivity → Resume CarPlay**, or run `carplay_projection resume`. This sends `requestUI` over the existing session. After an OEM return, a new `modesChanged` granting MainScreen to the phone can immediately reveal the retained frame if the same Type 110 stream and display layout are still active. A replaced stream or invalidated frame requires a newly decoded frame. The button remains available if the phone closes only the screen stream. Use `carplay_status` to inspect MainScreen ownership and `carplay_connection status` to check the session and video state. If the whole session ends, the connection manager handles reconnection. For a wired check, keep music playing through the OEM tile and resume round trip, and verify that the RTSP session and iAP2 link survive.

## Hardware composition and ownership

MPP continues to decode H.264 to its owned frame buffers. DE scales and positions the video layer. The GUI owner crops the existing opaque UI framebuffer to the right strip in FUSION and uses pixel alpha so the empty part of DE's full-panel UI surface remains transparent. Global alpha 255 made that area opaque on D213 and hid the video despite valid decode/layer counters. LOCAL restores the original geometry/alpha; FULLSCREEN uses global alpha zero. It preserves the current framebuffer physical address across LVGL buffer swaps. There is no added per-frame video software copy. This path requires the existing unrotated, uncropped 1024×600 UI layer; a geometry mismatch requests LOCAL and records an error.

Only a successful video layer update and VSYNC publish a new input rectangle. Static phone images are re-presented on mode changes without counting them as newly received frames. Failed swaps retain both potentially scanned frames. Shutdown/reset must disable the video layer and confirm VSYNC before returning frames or destroying MPP; failed fences retain ownership for retry. The UI policy attempts rollback on a layer/alpha/VSYNC transition failure and retries LOCAL from the GUI timer. Persistent display ioctl failure still requires diagnosis on the board; software cannot guarantee visible instruments after hardware failure.

Touch mapping uses the latched rectangle and negotiated logical dimensions. The CarPlay region tracks up to two independent contact IDs. The local UI keeps single-pointer ownership, and contacts cannot transfer between regions. Touches in the letterbox or in a pending layout are ignored. A phone contact that leaves the video is released at its last valid coordinates, while the other phone contact can continue. Mode, session and geometry changes release all captured contacts, including stationary fingers. Bounded state updates use short single-core interrupt masking; no LVGL, device ioctl, logging or external callback runs inside that span.

On the board, run `carplay_display status` before and after pinching entirely inside the projected map in Fusion mode. `two_finger_starts` increases when a second contact is routed to CarPlay. Use `carplay_status` to check for queue overflows or HID send failures and confirm zoom on the projected map.

## Vehicle provider and measurements

Speed, gear and state of charge initially show **SIMULATED DATA**. The ramp is a demo, not vehicle evidence. The application API `carplay_fusion_set_vehicle()` copies a validated `carplay_vehicle_sample_t` after initialization; set `available=false` to show unknown values. `carplay_fusion_use_simulated()` explicitly restores simulation. No CAN mapping or real vehicle timeout is guessed. Providers own their sampling/validity policy and should publish unavailability when their data is invalid. This API is independent of the portable Vehicle State Protocol model.

The dashboard updates every 250 ms, independently of connection-manager availability. It reports newly latched video FPS, drops, average queue/decode/present wall time, CPU load when the existing CPU usage package is enabled, and used heap when supported. Unavailable CPU/heap values show `n/a`. Re-presenting an old frame does not increase FPS. The existing video diagnostic shell retains detailed counters. These are receiver stage observations, not phone-to-glass latency; measuring end-to-end latency still needs a synchronized stimulus and external observation. A static video image alone is not treated as a failed connection, and local data keeps updating.

## Validation and remaining board work

`make -C application/rt-thread/rtcarplay check` includes actual coordinator, DE UI-policy and video-sink Host tests under ASan/UBSan. Coverage includes layout generations, coordinate boundaries, pointer ownership/cancellation, copied provider values, crop/alpha rollback, framebuffer address preservation, static-image mode changes, failure fences and repeat lifecycle regressions. `tests/launcher_preview` builds `launcher_preview` and `fusion_preview` against the actual LVGL 9 software renderer; the latter checks widget bounds, labels, mode buttons, provider unavailability and disconnect rendering. Its empty left viewport is not a captured phone image. The `D213 firmware` workflow runs both previews and uploads their PPM screenshots with the Host logs.

Native Windows GUI and Headless builds passed on 2026-11-15, with nine component bounds/CRC checks per complete image. The full application Host suite also checks Classic behavior. Its audio priority test retries only the documented nonblocking lock-busy result before asserting the priority result; production audio behavior is unchanged.

L2 remains **NOT_RUN** until the exact built image is flashed and physical composition/input is observed. Before flashing, re-read and verify private pairing/identity backups; restore them afterward and check automatic connection. Record source pins, clean/dirty inventory, image SHA256 and raw board logs. Physically test FULLSCREEN/FUSION/LOCAL, phone and local touch boundaries, unplug/reconnect, decoding failures, persistent instruments, framebuffer swaps and resource/performance behavior. Host success does not replace these checks. L3–L5 and complete official Ultra acceptance remain separate and NOT_RUN.

A historical board run confirmed FULLSCREEN video and restored the left FUSION video region with pixel alpha. Verify the current image on the panel, including the right instruments, touch, reconnect and fault recovery. See the [experimental acceptance criteria](ultra-development.md#current-scope-and-acceptance).
