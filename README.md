[English](README.md) | [简体中文](README.zh-CN.md)

# D213 CarPlay application

The application integrates the portable rt-carplay SDK into Luban-Lite on D213 with RT-Thread and a 1024×600 panel. GUI and Headless builds share a single [connection manager](docs/connection-manager.md) for automatic USB and wireless discovery. The GUI adds a [native LVGL dashboard and settings](docs/mobility-cockpit-v4.md).

## Source ownership

[Ultra experiments](docs/ultra-development.md) cover the portable protocol foundations, D213 hardware constraints and board acceptance criteria. Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI).

The unified D213 configuration can include an explicit, default-off [SIMULATED engine Vehicle State protocol bench](docs/vehicle-protocol-demo.md) for future phone interoperability tests. It does not expose the local dashboard's speed, gear or battery as guessed protocol characteristics, and does not enable UI Sync or official Local UI rendering.

| Directory | Responsibility |
| --- | --- |
| `app/` | Receiver composition, automatic connection manager, USB/iAP2, pairing and trust records |
| `platform/` | Board-specific Wi-Fi/Bluetooth/MFi/USB, video, audio/capture and touch |
| `ui/` | LVGL 9 Launcher V2 and host display policy |
| `tests/` | Actual-source protocol/worker/device-boundary tests and actual LVGL preview |
| `Kconfig` and `SConscript` | Application options and official SDK build integration |

The portable SDK remains the `packages/third-party/rt-carplay` Git submodule. Its root build entry provides generic protocols, runtime and ports; production builds do not compile SDK board examples. Independent Apple Ethernet support remains in `packages/third-party/cherryusb-ipheth`. The CherryUSB overlay owns SDK/baremetal integration, and `packages/third-party/cherryusb` retains the official 1.6.1 snapshot. Submodule URLs and exact revisions are in `.gitmodules` and Gitlinks.

## Build

Use the existing `carplay-oss` branch with the configured Linux toolchain/dependencies described in [CI](docs/ci.md). Populate all submodules first:

```sh
git submodule update --init --recursive
```

The maintained build helper builds the same-tree baremetal Bootloader, verifies the packaged binary, applies the application defconfig, builds and validates the complete IMG. Baremetal CherryUSB OSAL is implemented and tested; no manually generated address file or unverified prebuilt Bootloader is required.

```sh
bash .ci/build-firmware.sh
```

For manual local builds from the repository root:

```sh
scons --apply-def=d21x_demo128-nand-carplay_baremetal_bootloader_defconfig
scons -j8
scons --apply-def=d21x_demo128-nand_rt-thread_rtcarplay_defconfig
scons --info
scons -j8
```

After selecting a profile, normal incremental builds use `scons -j8`; repeatedly applying a defconfig resets local configuration. Images are under `output/{profile}/images/`. CI publishes the complete IMG, the A/B OTA package and the matching Bootloader as a rolling `preview` GitHub Release, with SHA256 checksums. A build proves neither flashing nor physical CarPlay operation.

## Run

The GUI defconfig selects `RTCARPLAY_DEMO_LAUNCHER`, which selects `RTCARPLAY_AUTO_CONNECT`. USB enumeration is armed before host initialization, while the application starts wireless discovery and arbitrates a single active session. No mode selection is required.

Use the manager's queued controls in automatic builds:

```text
carplay_connection status
carplay_connection retry
carplay_connection wireless off
carplay_connection wireless on
carplay_status
carplay_video status
carplay_bt status
carplay_iap2 status
ipheth_status
```

Settings are held in RAM and reset at reboot. Wireless off leaves USB enabled. Pairing and trust records persist independently. For manual bring-up, disable both automatic options before using `carplay_start wired` or `carplay_start`; do not mix manual lifecycle commands with an active manager.

Settings → About shows the Luban-Lite SDK version, rt-carplay SDK version, firmware commit and UTC build time. Settings → System update shows the installed firmware version, source commit and A/B slot. Check for updates reads a strict manifest containing version, full commit, board, package size and SHA256 over wired Ethernet. Any different commit is offered as a preview build, including an older commit. Installation requires confirmation of that exact target and an idle CarPlay session. The worker stages the complete package in `/data`, verifies the stored bytes, writes the inactive slot and reboots. Console commands `carplay_ota status`, `carplay_ota check` and `carplay_ota install` share the same worker gate. See [OTA update policy and validation](docs/ota.md).

## Classic CarPlay controls

Settings → Phone controls requests the iPhone's Automatic, Light or Dark appearance independently of the Launcher theme. Automatic uses the day setting on this board. The page shows song title, artist and playback state when the iPhone reports them. A queued command or successful socket write only confirms progress on the receiver side. Check the phone-rendered CarPlay UI and map to verify the change.

In wireless mode, the Demo receives Now Playing from AirPlay and from the Wi-Fi iAP2 tunnel. For diagnostics, start the serial capture before connecting, play a track, skip to the next one and pause, then run `carplay_status`. The `now_playing` lines identify whether `0x5000` was queued, which fields were decoded from `0x5001` or AirPlay updates, and what is present in the Demo cache. The lines omit track and artist text.

The wireless iAP2 tunnel also feeds the Route guidance card under Settings → Phone controls. Start a route on the iPhone, check the destination, current road and remaining distance when the phone supplies them, then end the route and disconnect to verify that the card clears. The `carplay route_guidance` line from `carplay_status` reports field presence.

Now Playing and Route guidance use the complete Noto Sans SC glyph map in both UI languages. See [font coverage and regeneration](ui/fonts/README.md).

### Simulated GNSS

On a D213 without a GPS module, open Settings → GNSS simulation. **Fixed point** uses point A. **A to B** takes two distinct, non-antipodal points and an integer speed from 1 to 300 km/h. Enter signed decimal degrees with up to six fractional digits. Choose **On** before connecting wirelessly, or reconnect if the current session was created with simulation off. The route starts at A on the first requested fix, follows a direct great-circle path, then keeps sending point B at zero speed. **Apply** updates the fix or restarts the route at A on the next sample. **Off** stops queuing new fixes. Configuration is kept in RAM and simulation is off after reboot.

The wireless iAP2 tunnel offers GGA/RMC at identification and queues one sentence per requested format per second. Moving RMC fixes include speed over ground in knots and true course. `carplay_gnss` shows configuration, capability offer, request mask (`0x1` GGA, `0x2` RMC), per-session queued/failed counts, clock source and motion state. `clock=fixture` uses a synthetic January 1, 2026 UTC date if the board clock is unset. To test phone delivery, open Maps under wireless CarPlay and observe the map while the motion state goes from `moving` to `arrived`. The counters show SDK queue acceptance. The phone decides whether to use the accessory's fix.

On D213ECV-DEMO V4.0, K4–K7 share one `KEYADC` resistor ladder on `PA.6`. See schematic sheets 2 and 6 and Hardware Manual §2.5.2 (printed page 9). The Demo defconfig enables `RTCARPLAY_D213_BOARD_KEYS` and GPAI channel 6, alternate function 2, with the internal pull disabled. `RESET`, `UBOOT` and `WAKEUP` retain their board functions. Each short press queues the following action during an active CarPlay session.

| PCB button | Drawing voltage | Demo action | RT-Thread serial command |
| --- | --- | --- | --- |
| K4 UP | 0.3 V | CarPlay Back | `carplay_phone_key back` |
| K5 DOWN | 0.6 V | CarPlay Home | `carplay_phone_key home` |
| K6 LEFT | 1.0 V | Siri | `carplay_siri` |
| K7 RIGHT | 1.3 V | Play or pause | `carplay_phone_key play-pause` |

Run `carplay_board_keys` to read the latest ADC count and decoded key at idle and with each button held. The initial windows use the BOM resistor ladder (10 kΩ pull-up, 1/2.2/4.7/6.8 kΩ segments), a 3.3 V pull-up supply and the SDK's 3.0 V, 12-bit GPAI scale. The drawing's printed voltages differ from the divider calculation, so verify the windows on the board. The adapter samples every 20 ms and requires three consecutive readings for both a press and a release. A held button fires once. The Receiver worker sends the phone command. The shell also accepts `carplay_phone_key previous` and `carplay_phone_key next`. The Fusion strip's Dashboard control returns to the local Launcher.

For D213 and iPhone acceptance, connect over USB and press each board key, including a long hold and a second press after release. Check the decoded counts with `carplay_board_keys` and use the table's serial commands to isolate phone-side behavior. Switch both phone appearances while a map is visible and confirm the Launcher keeps its own theme. While music plays, change the local volume through mute and back, check Now Playing through a track change, and trigger navigation, a call and Siri. Check audio ducking, recovery, microphone capture and sound quality. Disconnect USB during playback, reconnect the paired phone over wireless, and repeat after forcing a Wi-Fi disconnect. Check the connection manager with `carplay_connection status` and verify that the old playback metadata clears after disconnect.

## Application configuration

The `rt-carplay application` menu owns board options; generic SDK options have one owner in the SDK Kconfig. The GUI configuration enables the following features; Headless deliberately omits media/input devices.

| Option | GUI setting and purpose |
| --- | --- |
| `RTCARPLAY_DEMO_LAUNCHER` | LVGL 9 Launcher V2 |
| `RTCARPLAY_AUTO_CONNECT` | Automatic single-session USB/wireless manager |
| `RTCARPLAY_AUTO_START` | Disabled; separate manual Receiver autostart option |
| `RTCARPLAY_DIR` | `../../../packages/third-party/rt-carplay` relative to this directory |
| `RTCARPLAY_USB_HOST` | CherryUSB wired CarPlay |
| `RTCARPLAY_BT` / `RTCARPLAY_IAP2` | Bluetooth RFCOMM and wireless iAP2 |
| `RTCARPLAY_MFI` | Board MFi 2.0C provider |
| `RTCARPLAY_VIDEO` | MPP H.264 and host-owned display policy |
| `RTCARPLAY_AUDIO` | sound0 output with media buffering and shared-clock protection |
| `RTCARPLAY_MICROPHONE` | dmic0 uplink; installed acoustic verification required |
| `RTCARPLAY_MIC_AEC` | GUI enables queue-aligned AEC with runtime comparison/calibration |
| `RTCARPLAY_TOUCH_PANEL` | Exclusive panel reader and CarPlay HID routing |
| `RTCARPLAY_SCREEN_WIDTH/HEIGHT` | Fallback logical size 1024 / 600; the connection manager requests 768 / 600 in FUSION and 1024 / 600 in FULLSCREEN |
| `RTCARPLAY_OTA` | Settings firmware version and wired-Ethernet A/B update |

Pinmux, clocks and partition layout belong to [the board](../../../target/d21x/demo128-nand-carplay/). Verify board and application configuration together when changing media or input. Launcher V2 preserved the prior audio behavior. The subsequent [audio recovery](docs/audio-recovery.md) changes application buffering, clock protection and AEC integration; portable SDK protocol and codec behavior remain unchanged.

## Test and documentation

```sh
packages/third-party/rt-carplay/scripts/fetch-mbedtls.sh
make -C application/rt-thread/rtcarplay -j8 check
```

The Host suite runs actual sources with ASan/UBSan and modeled scheduling and device boundaries. The [LVGL preview](docs/mobility-cockpit-v4.md#evidence) also drives the real launcher UI. Record the image SHA-256 and source revision separately for each board result.

| Area | Guides |
| --- | --- |
| Build and maintenance | [Firmware CI and SDK rebase](docs/ci.md), [formatting](docs/code-formatting.md) |
| Connections | [Connection manager](docs/connection-manager.md), [wired startup](docs/wired-startup.md), [native NCM](docs/wired-native-ncm.md), [session snapshots](docs/session-snapshots.md) |
| Launcher and updates | [Native UI](docs/mobility-cockpit-v4.md), [single-screen fusion](docs/single-screen-fusion.md), [OTA](docs/ota.md) |
| Board diagnostics | [Audio recovery](docs/audio-recovery.md), [media and transport](docs/media-transport-repair.md), [Ethernet PHY](docs/ethernet-phy-verification.md), [Internet sharing](docs/internet-sharing.md), [USB files](docs/usb-files.md) |
| Experiments | [Ultra on D213](docs/ultra-development.md), [Vehicle State and UI Sync tools](docs/vehicle-protocol-demo.md) |
| Portable API | [rt-carplay SDK](../../../packages/third-party/rt-carplay/README.md) |

English guides in `docs/` are the primary documents. Their Simplified Chinese counterparts use the same filename in `docs/zh-CN/`. Directory entry pages use `README.md` and `README.zh-CN.md`. Update both languages together.
