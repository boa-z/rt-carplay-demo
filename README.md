[English](README.md) | [简体中文](README.zh-CN.md)

# D213 CarPlay integration

This application runs the portable rt-carplay SDK on an ArtInChip D213 with RT-Thread. The default firmware uses a 1024×600 LVGL launcher and automatically accepts USB and wireless CarPlay. Start with the [SDK integration guide](../../../packages/third-party/rt-carplay/docs/receiver-api.md) for the portable API and the [Classic capability matrix](../../../packages/third-party/rt-carplay/docs/classic-capability-audit.md) for feature status and physical acceptance.

## Build and connect

From the repository root, initialize the pinned dependencies and build the bootloader and IMG. Use the [build and CI guide](docs/ci.md#firmware-builds) for Linux prerequisites, output names and artifacts.

```sh
git submodule update --init --recursive
bash .ci/build-firmware.sh
```

The default profile is `d21x_demo128-nand_rt-thread_rtcarplay_defconfig`. For incremental builds after selecting it, run `scons -j8`. The package is for the D213 `demo128-nand-carplay` board with 128 MiB SPI NAND. After flashing, connect by USB or pair over Bluetooth. The [connection manager](docs/connection-manager.md) owns the single active projection session and can keep the other transport available. For standalone bring-up with automatic connection disabled, use the [wired startup guide](docs/wired-startup.md).

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
| Portable SDK | [rt-carplay](../../../packages/third-party/rt-carplay/README.md) | RTSP, pairing, encrypted media, iAP2, HID and the Receiver lifecycle |
| Application | [`app/carplay_app.c`](app/carplay_app.c), [`app/connection_manager.c`](app/connection_manager.c) | Single Receiver owner, transport arbitration, pairing storage, session snapshots and command queues |
| Board adapters | [`platform/`](platform/) | Bluetooth, Wi-Fi, USB, MFi, MPP/DE, speaker, microphone and touch |
| User interface | [`ui/lvgl_launcher.c`](ui/lvgl_launcher.c) | LVGL settings and dashboard, projection view and user requests |

The connection manager chooses a transport. The application poll worker owns Receiver calls. Media callbacks hand off to board workers, and the UI reads copied snapshots and queues user commands. USB networking is provided by the independently maintained `cherryusb-ipheth` or native NCM adapter. See [session snapshots](docs/session-snapshots.md) for ownership during teardown.

The local `Kconfig` and `SConscript` select board features. SDK protocol switches belong to the SDK's `Kconfig`. Keep device drivers and UI out of the portable Receiver and leave capability declarations aligned with installed backends.

## Use and diagnose

| Task | Guide |
| --- | --- |
| Now Playing, artwork, navigation, iPhone status, and RTC | [Phone Controls](docs/mobility-cockpit-v4.md#phone-controls) |
| Fullscreen, Fusion, touch mapping and 30/60 FPS | [Display guide](docs/single-screen-fusion.md) |
| USB enumeration, trust or network bring-up | [Wired startup](docs/wired-startup.md) and [native NCM](docs/wired-native-ncm.md) |
| Wireless pairing, reconnect and wired/wireless handoff | [Connection manager](docs/connection-manager.md) and [Bluetooth checks](docs/mobility-cockpit-v4.md#bluetooth-name) |
| Audio routing, Siri, and microphone recovery | [Audio recovery](docs/audio-recovery.md) |
| Network, USB files and firmware | [Network manager](docs/network-manager.md), [Internet sharing](docs/internet-sharing.md), [USB files](docs/usb-files.md), [A/B OTA](docs/ota.md) and [update mirrors](docs/ota-mirror.md) |

## Verify changes

```sh
make -C packages/third-party/rt-carplay -j8 check
make -C application/rt-thread/rtcarplay -j8 check
make -C application/rt-thread/rtcarplay check-docs
```

The SDK suite checks protocol and API behavior. The integration suite uses production source with Host device stubs. CI also renders the real LVGL launcher and Fusion strip. A build or Host check does not establish D213 and iPhone interoperability. Record physical tests against the firmware commit and identify wired and wireless results separately.

Update both languages for changes to public APIs, configuration or user-visible behavior. Update capability status in the SDK matrix and keep board procedures in the relevant integration guide.

## Investigations

[Media transport repair](docs/media-transport-repair.md) records historical board experiments. [Ultra development](docs/ultra-development.md) and the [Vehicle State bench](docs/vehicle-protocol-demo.md) are experimental. The default Classic integration uses the native LVGL dashboard.
