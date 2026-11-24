[English](mobility-cockpit-v4.md) | [简体中文](zh-CN/mobility-cockpit-v4.md)

# Mobility Cockpit V4 native UI

The V4 design package (`Mobility_Cockpit_UI/mobility_cockpit_v4`) replaces the Launcher V2 debug-first pages with four product-facing LVGL 9 screens at 1024×600. The change is confined to the existing `rtcarplay` application, its board pinmux and its defconfig. No new application, defconfig or SDK dependency is introduced, and the portable rt-carplay SDK remains a protocol library without LVGL or HMI code.

## Scope

| Screen | Content |
| --- | --- |
| Dashboard | Speed as the primary readout, state of charge with a percentage bar, connection stage and clock, Connectivity and Settings entries |
| Connectivity | CarPlay status, failure text, Connect/Retry/Resume action, pairing state; Android Auto and CarLife+ are labelled Planned and never simulate a backend |
| Settings | Media volume, display brightness, Appearance (light/dark; Auto disabled), Language, CarPlay display (Fullscreen/Fusion) and the System update entry |
| System update | Installed version and slot, real check/download/apply/reboot/failed states with progress when known, install confirmation, backend availability |

Removed with the previous demo UI: the standalone vehicle-data page, the vehicle-signal-anomaly page, the audio test page and its worker, the diagnostics page, the saved-device/file manager page, the vehicle-network page and the earlier three-mode display page. Their state machine, ownership locks and tests were deleted from `app/connection_manager.c`, `platform/audio_test_lubanlite.c` and `tests/unit/test_audio_diagnostics.c` rather than hidden behind a menu. FinSH diagnostic commands remain available to developers.

## Icons

`tools/generate_icon_assets.py` rasterises [Lucide](https://lucide.dev) glyphs (ISC) into single-colour A8 masks at the design sizes and emits `ui/icons/carplay_icons.c` / `.h`. The UI uses `lv_image_set_src()` and recolours the glyphs through the `CARPLAY_STYLE_ICON_*` theme roles via `image_recolor`. Regenerate with:

```sh
python -I tools/generate_icon_assets.py <path>/lucide.ttf
```

The generated file is excluded from clang-format (`.clang-format-ignore`), like the font subsets.

## Theme and language

`ui/carplay_ui_theme.c` holds the V4 palette as role tokens and applies a whole role to an object (background, border, text colour, radius-independent fill opacity) so a theme change restyles the widget tree without re-running layout. `carplay_ui_set_dark()` switches immediately; every page, the modals and the fusion strip are registered, and the choice is written to the settings record. Auto is present but disabled: the board has no ambient-light sensor, and the page states that rather than faking a schedule.

`ui/ui_language.c` maps each English label to its simplified-Chinese counterpart and switches live with `carplay_ui_set_chinese()`. CJK text is drawn with a Noto Sans SC subset at the matching size (16/18/24/30). All four CJK font files are generated from one union symbol set by `tools/generate_ui_fonts.py`, so a label can never render as missing-glyph boxes at one size only. English uses the Inter subsets produced by `tools/generate_display_fonts.py`.

Preferences persist through `app/launcher_preferences.c` in `/data/carplay-ui.bin`. The record stores numerical settings and the Bluetooth name with a checksum. A temporary file and backup protect replacement. Settings reports a failed save.

## Bluetooth name

Open **Settings → Bluetooth**, enter a name with 1–28 printable ASCII characters and tap **Apply**. Restart the board to update the controller's Classic local name and discovery EIR. The default is `rt-carplay`. The Bluetooth address, pairing records, Wi-Fi network name and Bonjour name retain their existing values. Check the log for `Bluetooth local name` and scan from the phone. A phone with a saved bond can continue to display its cached label.

If the iPhone lists the receiver but cannot connect, capture `carplay_bt status` before and after tapping the name. An idle receiver verifies page and inquiry scan every 10 seconds and retries a failed controller check after 2 seconds without stopping the listener. Look for `READY radio recovered`, then `BR incoming connection`, `incoming Classic page`, ACL, pairing and RFCOMM events. An attempt with no `BR incoming connection` has no incoming HCI event in the capture. Repeat with a retained bond and with a fresh pair.

## Volume and brightness

Both controls are bound to real backends and are disabled when the backend is absent: the value slot reads "Not supported", the hint row reads "Not supported on this device". Neither slider simulates a value. The slider value is the bare percentage used by the reference design (the card title names the quantity), so no language can overflow that slot; the OTA state line uses shortened copy for the same reason.

| Control | Backend | Contract |
| --- | --- | --- |
| Media volume | `platform/audio_volume_lubanlite.c` → `sound0`, `AUDIO_TYPE_MIXER` / `AUDIO_MIXER_VOLUME` | Range 0..100. The set performs `AUDIO_CTL_CONFIGURE`, then reads the value back; success requires the read-back to equal the request. A driver without mixer support answers the capability query negatively and the slider stays disabled. The AirPlay SET_PARAMETER volume is a separate remote control and is not overwritten. |
| Display brightness | `platform/backlight_lubanlite.c` → PWM channel 3 (PC.7 / PWM3_B) | Range 5..100. `rt_pwm_set` at 1 kHz followed by `rt_pwm_enable`; read-back converts `pulse/period` to a percentage. The minimum of 5 keeps a visible floor and avoids a blackout. |

The board schematic routes `GPC_P7/UART3_RTS/PWM3_B` as LCD_PWM into the LED boost converter, while `GPC_P6/SDC1_DET` is the SD card detect input. The previous image enabled the panel with a static GPIO level on PC.7 and left PC.6 unclaimed. Both the unified `d21x_demo128-nand_rt-thread_rtcarplay_defconfig` and the historical Ultra GUI profile selected `AIC_USING_PWM3`, `AIC_PWM_BACKLIGHT`, channel 3 and 1 kHz instead of `CONFIG_AIC_PANEL_ENABLE_GPIO="PC.7"`, and `target/d21x/demo128-nand-carplay/pinmux.c` claims only PC.7 for PWM3 so card hotplug keeps working on PC.6. The Ultra profiles were removed when the configurations were unified; the remaining defconfig carries those settings.

## Projection modes

The Settings page writes the display mode through `carplay_connection_set_display_mode()`, which the existing connection manager already implements: it closes USB/iAP2 borrowers, stops the old Receiver and recreates Receiver/HID/sink at the requested native geometry. Requests coalesce, an open device/audio tool can refuse a change, and the mode is persisted and restored before the connection worker starts. A build without `RTCARPLAY_SINGLE_SCREEN_FUSION` reports "Not supported on this device" instead of pretending the switch exists.

- **FULLSCREEN** requests native 1024×600 video. The LVGL layer is fully transparent and no local page, bar, back control or modal is drawn while video is latched.
- **FUSION** requests 768×600 video in the left viewport. The right 256×600 strip draws speed and state of charge only. The strip's `lv_obj_create(NULL)` screen is deliberately transparent and is not registered with the theme, because a themed background would repaint the video side on every theme change.

`ui/fusion_dashboard.c` draws inside a 256-pixel strip. `tests/launcher_preview/fusion_main.c` seeds the projection side with a known pattern and asserts, after every strip repaint, that the bytes are unchanged and that every strip widget's absolute bounds satisfy `x >= 768`. A touch at x=767 is outside the strip and a touch at x=768 is inside it.

## OTA

The page controls `carplay_ota_*` in `platform/ota_lubanlite.c`. It shows the installed version, slot, saved `.cpio` URL, and download and installation progress when available. Download, package verification and installation run in a worker. The page asks for confirmation with the verified target version before installation. See [Firmware updates](ota.md).

## Phone Controls

Wired USB iAP2 and wireless AirPlay iAP2 use the same subscriptions for Now Playing, route guidance and iPhone status. `carplay_classic_diag on` enables serial diagnostics for either transport.

Open **Settings → Phone Controls** to see Now Playing, route guidance and phone status. Now Playing displays the received track, artist, album, playback state, elapsed time and duration. The D213 Demo accepts artwork up to 192 KiB from wired iAP2, the wireless iAP2 tunnel or an inline AirPlay update. It decodes baseline JPEG up to 2048×2048 directly into a thumbnail of at most 96×96 pixels and accepts PNG up to 512×512. When a cover is unavailable it displays a music icon. The navigation card shows the reported destination, current road, remaining distance, next-turn distance, up to three announced maneuvers in order and up to eight lane indicators. The status card displays the reported battery, charging, cellular and call information when present. It shows local board RTC time and distinguishes manual mode, waiting for iPhone time and receipt of a phone time sample. **Settings → Date & time** controls how the board clock is set.

**Implemented / Host Tested.** The integration and LVGL preview exercise field clearing and artwork fallback. iAP2 title and playback updates preserve the current cover when they omit artwork. A new transfer can clear the previous cover until its replacement arrives. **Hardware Pending.** On D213, connect an iPhone over USB and then wirelessly. Verify that the cover stays visible through title and playback updates and changes when the phone sends a replacement. Start and cancel a route with upcoming turns and lane guidance. Check phone status during charging and a call, verify local time and the selected clock mode, then disconnect and reconnect. Session data should clear on disconnect and return as updates arrive. The board RTC should keep running.

### Clock and location simulation

In **Settings → Date & time**, **Sync with iPhone** accepts `DeviceTimeUpdate` from USB or wireless, stores UTC in the board RTC and applies the reported offset for display. **Manual** ignores incoming updates until automatic mode is selected again. Reconnect if the phone only supplies a time sample at session startup.

For a bench without GNSS hardware, open **Settings → GNSS simulation** before connecting wirelessly. Select **Fixed point** or **A to B**, enter signed decimal coordinates and a speed from 1 to 300 km/h for motion. `carplay_gnss` reports offered formats, the phone subscription and queued/failed samples. The generator stops new fixes when switched off. The phone decides whether to use the accessory's location.

### D213 board keys

The default D213 board maps K4 to Back, K5 to Home, K6 to Siri and K7 to Play/Pause. `carplay_board_keys` reports ADC readings and the decoded key. `carplay_siri` and `carplay_phone_key` let you check the corresponding phone actions independently of the ADC adapter.

## Screen recovery

The D213 video queue accepts or drops entire H.264 access units. The decoder can continue while the display engine latches a submitted frame. The sink retains the current scanout, one submitted frame awaiting a display interrupt, and the newest decoded frame awaiting submission. A newer decoded frame can replace the unsent frame. The previous scanout returns to MPP only after the display fence, and stopping or rebuilding the decoder still requires a layer-disable fence. On overflow, the sink discards queued pictures, retains the latest queued SPS/PPS, and reserves room for the next access unit that starts with the first slice of an IDR picture. The receiver requests a MainScreen keyframe over the encrypted event channel. A complete IDR refreshes the decoder after a queue gap. Decoder errors and changed stream configuration still require a rebuild.

**Host Tested.** The normal, Launcher and Fusion sink regressions cover overflow, IDR admission, display interrupt sequencing, and buffer retention on fence failure. **Hardware Pending.** Repeatedly invoke Siri over wired and wireless projection at 60 fps and use 30 fps as a control. Take `carplay_video status` snapshots just before and after the run. `async_vsync=1` indicates that the driver supports nonblocking fence checks. Compare the change in `overflows`, which is cumulative within a session. `overflow_to_idr_ms` measures the last and maximum time from queue overflow to submission of the first IDR slice to MPP. `idr_to_latch_ms` measures the time from that submission to the first corresponding or later frame confirmed by VSync. `presented_fps` averages over the entire interval between snapshots, including periods when the iPhone sends no new picture. If a gap occurs, check the recovery timings and `resets`, and confirm that the picture resumes in the same session without repeated flashing.

In asynchronous mode, only a nearly full queue limits display submissions to one every 20 ms. Pacing begins at 12 units, or at 10 units with an oldest unit age of at least 120 ms. Normal cadence resumes below 10 units regardless of age. `pressure_peak=1` records moderate backlog with no submission cap. The newest unsent frame replaces older output, and MPP frame starvation may release a deferred unsent frame. The first frame and IDR recovery bypass pacing. The thresholds are provisional until measured on D213.

Use `carplay_perf on` to capture approximately five-second windows and `carplay_perf off` to stop reporting. `show` counts VSync-confirmed frames. Compare it to `rx` alongside queue drops and `rx_latch_ms`, which measures P95/max from receipt of a complete screen message to confirmation of that frame's latch. Its sample count includes only latched frames. `deferred` counts unsent frames that encountered pacing and `discarded` counts decoded output discarded before submission. `peak_window`, queue and MPP decode P95/max, packet and frame pressure, display submission and fence times, and producer allocation, copy and mutex maxima describe the current window. `peak_total` remains cumulative. P95 is a histogram estimate. MPP decode and display timings are wall times and can overlap. The overflow line records the last full-queue event and the pressure counts at that point in its window.

## Evidence

Render the current UI using the LVGL 9 software renderer at 1024×600. The launcher preview covers settings and the GNSS editor. The fusion preview covers the instrument strip and full-screen transitions.

```sh
cmake -S application/rt-thread/rtcarplay/tests/launcher_preview -B build/launcher-preview -DCMAKE_BUILD_TYPE=Release
cmake --build build/launcher-preview -j8
mkdir -p build/launcher-v4-shots
build/launcher-preview/launcher_preview build/launcher-v4-shots
build/launcher-preview/fusion_preview build/launcher-v4-shots
```

These Host renderings use modeled connection and device values. On the board, verify the panel backlight, codec volume, projection modes and OTA results separately with the exact image and corresponding logs.
