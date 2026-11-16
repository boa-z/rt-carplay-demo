[English](ci.md) | [简体中文](zh-CN/ci.md)

# Repository, firmware CI and upstream

The maintained SDK integration is [boa-z/luban-lite-carplay](https://github.com/boa-z/luban-lite-carplay), an independent repository with `carplay-oss` as its development and default branch. It starts from `boa-z/luban-lite` commit `4060f85bd27a1eb555952921139b77cc41bdeb4e` and records the existing public CarPlay integration as a separate commit. It does not import the unrelated development history of `jiachenin/luban-lite-jc`.

```sh
git clone --branch carplay-oss --recurse-submodules https://github.com/boa-z/luban-lite-carplay.git
cd luban-lite-carplay
git remote add upstream https://github.com/boa-z/luban-lite.git
git fetch upstream master
```

The upstream default branch is `master`. Fetching upstream does not automatically change the integration branch or its pinned submodules. Review SDK updates and build the firmware before incorporating them. The repository does not automatically open pull requests.

## Firmware builds

The `D213 firmware` GitHub Actions workflow runs on pushes to `carplay-oss` and supports manual dispatch. It first runs the SDK and application Host regressions plus both actual-LVGL previews, then builds the single unified firmware and publishes the resulting IMG, A/B OTA package and Bootloader as a rolling `preview` GitHub Release. A push whose commit message contains `[ultra-dev]` skips only the release step: the image is still built and uploaded as an Actions artifact for review. Checkout enables Git LFS and recursive submodules; the SDK contains the Xuantie V2.6.1 Linux toolchain archive and the required Realtek library as LFS objects.

```sh
python3 -m venv .ci-venv
. .ci-venv/bin/activate
pip install -r .ci/requirements.txt
bash .ci/build-firmware.sh
```

Install `build-essential`, `patch`, `dosfstools`, `mtools`, `device-tree-compiler`, `u-boot-tools` and `cpio` on Ubuntu. The build script first compiles `d21x_demo128-nand-carplay_baremetal_bootloader_defconfig` from this repository. It verifies that the packaged Bootloader is byte-identical to the newly built binary. It then applies `d21x_demo128-nand_rt-thread_rtcarplay_defconfig` and produces the complete ArtInChip `.img` container. It does not rely on a prebuilt Bootloader or a second SDK checkout. Before upload, `.ci/verify-image.py` checks the container bounds, all nine component CRC32 values and the mandatory DDR updater, Bootloader and OS records.

The workflow publishes `d213-carplay.img`, the A/B OTA package `ota.cpio`, the matching Bootloader, `version.txt` (the manifest with `version=`, `commit=`, `board=`, `size=` and `sha256=` read by Settings), `source-manifest.txt` and `SHA256SUMS` to the rolling `preview` prerelease. Download them from there and run `sha256sum -c SHA256SUMS` before flashing with the board's ArtInChip tool. These are D213 `demo128-nand-carplay` images for the existing 128 MiB SPI NAND board and panel configuration. The OTA package keeps the same URL across builds.

Build logs are uploaded on failure. A successful workflow confirms cross-compilation and packaging; physical USB upgrade, NAND boot, panel, audio and iPhone acceptance require a separate board run and are recorded as `NOT_RUN` in the source manifest.

## CherryUSB baremetal integration

The SDK overlay in `packages/third-party/cherryusb-luban` supplies an interrupt-safe semaphore and bounded queue implementation, foreground timers, Hub polling, SDK upgrade compatibility functions and the ArtInChip upgrade descriptor callbacks. It uses SDK allocation, time, critical-section and delay APIs. It does not pretend to create threads. The generated Hub build copy replaces its thread with `usbh_hub_poll()` only for `KERNEL_BAREMETAL`; the RT-Thread Hub source remains unchanged.

Synchronous EHCI transfer waits leave interrupts enabled and return `-USB_ERR_TIMEOUT` on expiry. Foreground Hub polling handles connection notifications and enumeration. Timer callbacks run in the foreground; start/stop can be signalled from an IRQ. Timer creation/deletion and Hub polling must remain in the single foreground owner. Completion producers must be stopped before deleting their timer or synchronization objects. Hub deinitialization releases child interfaces before stopping the controller and releasing the queue/semaphore.

The Bootloader retains the existing USB Device upgrade, USB mass-storage upgrade and SD-card upgrade options. Its manual USB-role policy remains separate from an application using automatic OTG. Applications must keep calling `usbh_hub_poll()` and the existing OTG poll entry from their main loop. RTOS threads, RTOS class examples and arbitrary background tasks are not provided by this polling port.

```sh
bash packages/third-party/cherryusb-luban/tests/run_baremetal.sh
bash packages/third-party/cherryusb-luban/tests/run.sh
bash packages/third-party/cherryusb-luban/tests/run_control_probe.sh
```

The tests compile actual adapter sources with ASan/UBSan. They verify software contracts and do not simulate successful board enumeration or flashing.

## Video display regression

Run `make -C application/rt-thread/rtcarplay check-lubanlite-video SDK_ROOT="$PWD"` when changing the video path. This covers the actual MPP sink and host UI policy, including the SDK framebuffer command ABI. See the [video handoff fix](video-handoff-fix.md) for the confirmed command mismatch and the required board acceptance, and the [migration verification report](repository-migration-verification.md) for firmware identities and corrected user feedback.

## Host-owned platform regression suite

Run `make -C application/rt-thread/rtcarplay -j8 check` under ASan/UBSan after fetching the SDK's pinned mbedTLS. It includes the relocated receiver/audio/pairing tests and the existing video, touch, launcher, bonds, USB DMA, handoff, reconnect and capability tests. The SDK's standalone suite no longer compiles board sources. Generic SDK switches are sourced once from its Kconfig; the application owns hardware selection and source collection. See the SDK [migration guide](../../../../packages/third-party/rt-carplay/docs/luban-lite-migration.md). These are Host device/scheduler models, not physical board acceptance.

CI validates the complete CRC CPIO archive and restricts its mapping to OS and rodata before generating the authorization manifest. The manifest is uploaded after the assets. A rolling-release replacement during transfer fails size or SHA256 validation; check again to obtain a consistent target. See [OTA](ota.md).
