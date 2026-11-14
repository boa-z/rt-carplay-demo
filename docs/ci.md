[English](ci.md) | [简体中文](zh-CN/ci.md)

# Repository, firmware CI and upstream

The maintained SDK integration is [boa-z/luban-lite-carplay](https://github.com/boa-z/luban-lite-carplay), an independent repository with `carplay-oss` as its development and default branch. It starts from `boa-z/luban-lite` commit `4060f85bd27a1eb555952921139b77cc41bdeb4e` and records the existing public CarPlay integration as a separate commit. It does not import the unrelated development history of `jiachenin/luban-lite-jc`.

```sh
git clone --branch carplay-oss --recurse-submodules https://github.com/boa-z/luban-lite-carplay.git
cd luban-lite-carplay
git remote add upstream https://github.com/boa-z/luban-lite.git
git fetch upstream master
```

The upstream default branch is `master`. Fetching upstream does not automatically change the integration branch or its pinned submodules. Review SDK updates and run both firmware variants before incorporating them. The repository does not automatically open pull requests.

## Firmware builds

The `D213 firmware` GitHub Actions workflow runs on pushes to `carplay-oss` and supports manual dispatch. Independent Ubuntu 24.04 jobs build the GUI and Headless variants. Checkout enables Git LFS and recursive submodules; the SDK contains the Xuantie V2.6.1 Linux toolchain archive and the required Realtek library as LFS objects.

```sh
python3 -m venv .ci-venv
. .ci-venv/bin/activate
pip install -r .ci/requirements.txt
bash .ci/build-firmware.sh gui
# Use a separate clean checkout for the other variant.
bash .ci/build-firmware.sh headless
```

Install `build-essential`, `patch`, `dosfstools`, `mtools`, `device-tree-compiler` and `u-boot-tools` on Ubuntu. The build script first compiles `d21x_demo128-nand-carplay_baremetal_bootloader_defconfig` from this repository. It verifies that the packaged Bootloader is byte-identical to the newly built binary. It then applies the selected RT-Thread defconfig and produces the complete ArtInChip `.img` container. It does not rely on a prebuilt Bootloader or a second SDK checkout. Before upload, `.ci/verify-image.py` checks the container bounds, all nine component CRC32 values and the mandatory DDR updater, Bootloader and OS records.

Artifacts contain `d213-carplay-gui.img` or `d213-carplay-headless.img`, the matching Bootloader, firmware configuration, resolved image layout, source/submodule identities and `SHA256SUMS`. Download and unpack the corresponding workflow artifact, then run `sha256sum -c SHA256SUMS` before flashing with the board's ArtInChip tool. These are D213 `demo128-nand-carplay` images for the existing 128 MiB SPI NAND board and panel configuration; select the correct variant and board.

Artifacts are retained for 14 days. Build logs are uploaded on failure. A successful workflow confirms cross-compilation and packaging; physical USB upgrade, NAND boot, panel, audio and iPhone acceptance require a separate board run and are recorded as `NOT_RUN` in the source manifest.

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
