[English](README.md) | [简体中文](README.zh-CN.md)

# D213 CarPlay application

The application integrates the independent portable rt-carplay SDK into Luban-Lite on D213 with RT-Thread and a 1024×600 panel. [V2.1 verification](docs/refactoring-v21-verification.md) records the completed software architecture/lifecycle baseline. [Launcher V2](docs/launcher-v2.md) adds automatic USB/wireless discovery, a single session owner shared by GUI and Headless, and the LVGL 9 dashboard. Current source/image hardware acceptance remains NOT_RUN.

## Source ownership

[Ultra development](docs/ultra-development.md) records the LIVI source review, portable SDK milestone, dedicated GUI/Headless profiles, CI artifacts and separate L0-L5 acceptance. The final objective is complete official Ultra; the current profile is a development bench with unimplemented features kept out of capability declarations. Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI).

The dedicated Ultra profiles now include an explicit [SIMULATED engine Vehicle State protocol bench](docs/vehicle-protocol-demo.md) for future phone interoperability tests. Ordinary Classic profiles keep it disabled. It does not expose the local dashboard's speed, gear or battery as guessed protocol characteristics, and does not enable UI Sync or official Local UI rendering.

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

Settings → FIRMWARE & UPDATES shows the installed version, source commit and A/B slot. Check for updates reads a strict manifest containing version, full commit, board, package size and SHA256 over wired Ethernet. Any different commit is offered as a preview build, including an older commit. Installation requires confirmation of that exact target and an idle CarPlay session. The worker stages the complete package in `/data`, verifies the stored bytes, writes the inactive slot and reboots. Console commands `carplay_ota status`, `carplay_ota check` and `carplay_ota install` share the same worker gate. See [OTA update policy and validation](docs/ota.md).

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
| `RTCARPLAY_SCREEN_WIDTH/HEIGHT` | 1024 / 600, matching the panel |
| `RTCARPLAY_OTA` | Settings firmware version and wired-Ethernet A/B update |

Pinmux, clocks and partition layout belong to [the board](../../../target/d21x/demo128-nand-carplay/). Verify board and application configuration together when changing media or input. Launcher V2 preserved the prior audio behavior. The subsequent [audio recovery](docs/audio-recovery.md) changes application buffering, clock protection and AEC integration; portable SDK protocol and codec behavior remain unchanged.

## Test and documentation

```sh
packages/third-party/rt-carplay/scripts/fetch-mbedtls.sh
make -C application/rt-thread/rtcarplay -j8 check
```

The host suite runs actual sources with ASan/UBSan and modeled scheduling/device boundaries. The [Launcher preview](docs/launcher-v2.md) additionally builds actual LVGL and exercises UI events. The independent SDK and IPHETH repositories retain their own Make/CMake/CTest and bilingual-documentation gates.

| Document | Scope |
| --- | --- |
| [Wired startup and pairing](docs/wired-startup.md) | Concurrent CarKit/network startup, deferred invitation, RSA optimization and board checks |
| [Native wired NCM integration](docs/wired-native-ncm.md) | Exclusive network ownership, module boundaries, three-phone acceptance and regression limits |
| [EHCI completion recovery](docs/ehci-completion-recovery.md) | Reproduced async-advance reclamation defect, wired crash evidence and validation limits |
| [Wired video receive progress](docs/wired-video-recovery.md) | RX/TX lock isolation, queued lwIP input and board performance comparison |
| [mbedTLS 4.x assessment](docs/mbedtls-upgrade-assessment.md) | Measured MPI evidence, migration costs and performance limits |
| [Media and transport repair](docs/media-transport-repair.md) | Replacement music, echo and wired transport changes; acceptance boundaries |
| [Audio recovery](docs/audio-recovery.md) | Call echo, music scheduling, runtime diagnostics and board acceptance |
| [Ethernet Internet sharing](docs/internet-sharing.md) | Optional IPv4 gateway, protocol evidence, recovery and required iPhone acceptance |
| [V2 verification](docs/v2-verification.md) | Verified revisions, CI, IMG checksums and remaining board acceptance |
| [Connection manager](docs/connection-manager.md) | Arbitration, retry, settings, ownership and Headless behavior |
| [Launcher V2](docs/launcher-v2.md) | Pages, real LVGL preview and hardware checklist |
| [V2.1 verification](docs/refactoring-v21-verification.md) | Architecture, migration, lifecycle, bilingual audit and SDK repository cleanup |
| [Session snapshots](docs/session-snapshots.md) | Concurrency, retained ownership, callback lifetime and actual-source regressions |
| [CI](docs/ci.md) | Upstream, toolchain, build/package and evidence |
| [Historical bring-up](docs/build.md) | Earlier investigations and board reports; not the current build authority |
| [Portable SDK](../../../packages/third-party/rt-carplay/README.md) | Public APIs, Linux/RT-Thread ports, media and audio handoff |

Development uses existing `carplay-oss` and SDK `main` branches, with boa-z identity and no PR creation. The prior SDK repository was backed up and re-created at the owner's request; old Gitlinks/CI references require the verified offline backup. Current submodule commits are published and fetchable.
