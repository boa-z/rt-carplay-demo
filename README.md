[English](README.md) | [简体中文](README.zh-CN.md)

# rt-carplay-demo

This C and LVGL reference application integrates and validates the [rt-carplay SDK](https://github.com/boa-z/rt-carplay). The D213 baseline runs RT-Thread with RTL8733BS and a 1024×600 launcher for USB and wireless CarPlay. Linux currently has Host models and an LVGL Preview. A Linux Receiver is planned.

## Build and connect

From the [Luban-Lite integration checkout](https://github.com/boa-z/luban-lite-carplay), initialize the pinned submodules and build the bootloader, IMG and OTA CPIO. The [build and CI guide](docs/ci.md#firmware-builds) lists prerequisites and artifacts.

```sh
git submodule update --init --recursive
bash .ci/build-firmware.sh
```

The two D213 `demo128-nand-carplay` profiles use 128 MiB SPI NAND. In the integration repository run `bash .ci/build-firmware.sh d213-demo128-nand-carplay-rtl8733bs` or use target `d213-demo128-nand-carplay-aic8800d40`. Each produces a target-specific IMG and A/B OTA CPIO. For incremental builds after selecting a defconfig, run `scons -j8`. After flashing, connect by USB or pair over Bluetooth. The [connection manager](docs/connection-manager.md) owns the single active projection session and can keep the other transport available. For standalone bring-up with automatic connection disabled, use the [wired startup guide](docs/wired-startup.md).

```text
carplay_connection status
carplay_status
carplay_video status
carplay_bt status
carplay_iap2 status
```

## How the pieces fit

| Layer | Starting point | Owns |
| --- | --- | --- |
| Portable SDK | [rt-carplay](https://github.com/boa-z/rt-carplay) | RTSP, pairing, encrypted media, iAP2, HID and the Receiver lifecycle |
| Application | [`app/carplay_app.c`](app/carplay_app.c), [`app/connection_manager.c`](app/connection_manager.c) | Single Receiver owner, transport arbitration, pairing storage, session snapshots and command queues |
| Board adapters | [`platform/`](platform/) | Bluetooth, Wi-Fi, USB, MFi, MPP/DE, speaker, microphone and touch |
| User interface | [`ui/lvgl_launcher.c`](ui/lvgl_launcher.c) | LVGL settings and dashboard, projection view and user requests |

The connection manager chooses a transport. The application poll worker owns Receiver calls. Media callbacks hand off to board workers, and the UI reads copied snapshots and queues user commands. USB networking is provided by the independently maintained `cherryusb-ipheth` or native NCM adapter. See [session snapshots](docs/session-snapshots.md) for ownership during teardown.

The local `Kconfig` and `SConscript` select board features. SDK protocol switches belong to the SDK's `Kconfig`. See the [platform guide](docs/platform.md) for ownership, internal dependencies and the Linux and wireless roadmap.

## Use and diagnose

| Task | Guide |
| --- | --- |
| Now Playing, artwork, navigation, iPhone status, and RTC | [Phone Hub](docs/mobility-cockpit-v4.md#phone-hub) |
| System Shell, layouts, themes and fonts | [Cockpit design system](docs/design-system.md) |
| Fullscreen, Fusion, touch mapping and 30/60 FPS | [Display guide](docs/single-screen-fusion.md) |
| USB enumeration, trust or network bring-up | [Wired startup](docs/wired-startup.md) and [native NCM](docs/wired-native-ncm.md) |
| Wireless pairing, reconnect and wired/wireless handoff | [Connection manager](docs/connection-manager.md) and [Bluetooth checks](docs/mobility-cockpit-v4.md#bluetooth-name) |
| Audio routing, Siri, and microphone recovery | [Audio recovery](docs/audio-recovery.md) |
| AIC8800D40 and RTL8733BS SoftAP and Wi-Fi maintenance | [Platform guide](docs/platform.md) |
| Network, USB files and firmware | [Network manager](docs/network-manager.md), [Internet sharing](docs/internet-sharing.md), [USB files](docs/usb-files.md), [A/B OTA](docs/ota.md) and [update mirrors](docs/ota-mirror.md) |

## Verify changes

```sh
export SDK_ROOT=/path/to/luban-lite-carplay
bash "$SDK_ROOT/packages/third-party/rt-carplay/scripts/fetch-mbedtls.sh"
make -j8 check
make check-docs
```

See the [validation guide](docs/validation.md) for the hardware matrix and physical acceptance checklist. The [CI guide](docs/ci.md#standalone-host-validation) describes pinned dependencies and Preview.

Demo releases have their own version and Git tags. Use the integration and dependency commits in the firmware manifest to identify a build. Update both languages when public APIs, configuration or user-visible behavior changes. See [NOTICE](NOTICE) and [COPYING](COPYING) for licensing and attribution.

## Investigations

[Media transport repair](docs/media-transport-repair.md) records historical board experiments. [Ultra development](docs/ultra-development.md) and the [Vehicle State bench](docs/vehicle-protocol-demo.md) are experimental. The default Classic integration uses the native LVGL dashboard.
