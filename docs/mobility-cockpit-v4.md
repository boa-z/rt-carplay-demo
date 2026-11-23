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

The page reads and controls the existing `carplay_ota_*` service only (`platform/ota_lubanlite.c`). It shows the firmware-provided installed version and slot, and the service's checking/up-to-date/available/downloading/applying/rebooting/failed states with progress when known. Check and install are queued, never waited on inside the LVGL task, and Install asks for confirmation with the target version. When the build has no OTA service, or the device has no network, the controls are disabled and the backend line says unavailable. No endpoint, partition or signature check is invented, and no UI timer fabricates a successful flash.

## Phone Controls

Wired USB iAP2 and wireless AirPlay iAP2 use the same subscriptions for Now Playing, route guidance and iPhone status. `carplay_classic_diag on` enables serial diagnostics for either transport.

Open **Settings → Phone Controls** to see Now Playing, route guidance and phone status. Now Playing displays the received track, artist, album, playback state, elapsed time and duration. The D213 Demo accepts AirPlay PNG and baseline JPEG artwork up to 96 KiB and 384×384 pixels. When a cover is unavailable it displays a music icon. The navigation card shows the reported destination, current road, remaining distance, next-turn distance, up to three announced maneuvers in order and up to eight lane indicators. The status card displays the reported battery, charging, cellular and call information when present. It shows local board RTC time and distinguishes manual mode, waiting for iPhone time and receipt of a phone time sample. **Settings → Date & time** controls how the board clock is set.

**Implemented / Host Tested.** The integration and LVGL preview exercise these display paths with modeled values, including field clearing and artwork fallback. **Hardware Pending.** On D213, connect an iPhone over USB and then wirelessly. Change tracks, pause playback, and check received artwork and progress. Start and cancel a route with upcoming turns and lane guidance. Check phone status during charging and a call, verify local time and the selected clock mode, then disconnect and reconnect. Session data should clear on disconnect and return as updates arrive. The board RTC should keep running.

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
