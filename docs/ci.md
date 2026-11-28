[English](ci.md) | [简体中文](zh-CN/ci.md)

# Repository, firmware CI and upstream

The maintained SDK integration is [boa-z/luban-lite-carplay](https://github.com/boa-z/luban-lite-carplay), an independent repository with `carplay` as its default branch. `codex/androidauto-integration` shares the original CarPlay baseline. The upstream for both branches is the SDK snapshot mirror at `boa-z/luban-lite`.

```sh
git clone --branch carplay --recurse-submodules https://github.com/boa-z/luban-lite-carplay.git
cd luban-lite-carplay
git remote add upstream https://github.com/boa-z/luban-lite.git
git fetch upstream master
```

## SDK baseline and rebase

The Luban-Lite v1.3.3 baseline is mirror commit `90a740fe9c1b663ea52ec3f7e1af35acde200aea`, corresponding to ArtInChip Gitee commit `4bd5a1300f4793a2188529eebde96f012c2188bb`. Identify the SDK independently of the firmware commit and the `rt-carplay` submodule pin.

The mirror can replace `master` with an unrelated root commit. Keep each reviewed SDK snapshot under an immutable mirror tag keyed by its Gitee commit so collaborators can fetch it after `master` moves. Before fetching, record the SDK root of the current CarPlay branch and both product tips. Keep the old tips under backup refs or tags until verification and publication are complete. Use a disposable checkout on a case-sensitive filesystem, such as the Linux filesystem in WSL. This keeps build-generated configuration and bootloader files out of the active checkout and allows both upstream README paths to be checked out.

```sh
old_carplay=$(git rev-parse carplay)
old_projection=$(git rev-parse codex/androidauto-integration)
old_sdk=$(git rev-list --max-parents=0 "$old_carplay")
git merge-base --is-ancestor "$old_carplay" "$old_projection"
git fetch upstream master
new_sdk=$(git rev-parse upstream/master)
git log -1 --format='%H %s' "$new_sdk"
git diff --stat "$old_sdk" "$new_sdk"
```

Check that `old_sdk` is a single commit, the ancestor check passes, and `new_sdk` identifies the intended upstream snapshot. Find the existing README compatibility commit immediately after `old_sdk` and verify its diff. If the new snapshot still contains both `README.md` and `ReadMe.md`, prepare the new base in the disposable checkout:

```sh
git switch --detach "$new_sdk"
git mv ReadMe.md README.zh-CN.md
git commit -m 'chore: rename ReadMe.md to README.zh-CN.md to fix case conflict'
new_base=$(git rev-parse HEAD)
```

With `old_base` set to the old compatibility commit and `new_base` set to the new one, replay the product commits in order. Use the corresponding SDK commit as the boundary if that snapshot needs no compatibility commit. Run these commands from the disposable checkout after recording both boundaries:

```sh
git switch --detach "$old_carplay"
git rebase --no-update-refs --onto "$new_base" "$old_base"
new_carplay=$(git rev-parse HEAD)
git switch --detach "$old_projection"
git rebase --no-update-refs --onto "$new_carplay" "$old_carplay"
new_projection=$(git rev-parse HEAD)
git range-diff "$old_base..$old_carplay" "$new_base..$new_carplay"
git range-diff "$old_carplay..$old_projection" "$new_carplay..$new_projection"
```

Verify that the new SDK is an ancestor of `new_carplay` and that `new_carplay` is an ancestor of `new_projection`. Review any changed or dropped patches in `range-diff`. Check that both branch tips contain `README.md` and `README.zh-CN.md`, and compare each branch's submodule gitlinks against its previous tip. Initialize the pinned submodules and materialize Git LFS objects before building. Update submodule pins in separate product commits when required.

Run the CarPlay host regressions and LVGL previews, build the D213 bootloader and CarPlay image, and build both `rtprojection` and `rtprojection_wireless` when rebasing the Android Auto branch. Record the SDK commit, product commit, submodule commits, defconfig, and build and board results. Coordinate any rewrite of published branches with collaborators and check the expected remote tips before a lease-protected force push. A push to `carplay` can replace the rolling `nightly` release.

## Firmware builds

Run these commands in the integration checkout after `git submodule update --init --recursive`. Its `D213 firmware` workflow validates pull requests, pushes to `carplay` and manual builds. Only `carplay` publishes `nightly`. A push with `[ultra-dev]` still uploads Actions artifacts and skips the release. Git LFS supplies the Xuantie V2.6.1 Linux toolchain and Realtek library.

```sh
python3 -m venv .ci-venv
. .ci-venv/bin/activate
pip install -r .ci/requirements.txt
bash .ci/build-firmware.sh
```

Install `build-essential`, `patch`, `dosfstools`, `mtools`, `device-tree-compiler`, `u-boot-tools` and `cpio` on Ubuntu. The build script compiles `d21x_demo128-nand-carplay_baremetal_bootloader_defconfig` and verifies that the packaged Bootloader matches the new binary. It then applies the `d21x_demo128-nand_rt-thread_rtcarplay_rtl8733bs_defconfig` or `d21x_demo128-nand_rt-thread_rtcarplay_aic8800d40_defconfig` profile. The SDK writes the module name into the IMG filename, and CI checks component bounds and CRC32. RTL8733BS also produces `ota.cpio`. CI compares its version and OS/Rodata payloads with the IMG and runs the SDK downloader against modeled NAND.

The release keeps the SDK filenames for both the USB IMG and [OTA package](ota.md). `SHA256SUMS` covers both files and `source-manifest.txt`. After downloading the assets, run `sha256sum -c SHA256SUMS` to verify them.

Build logs are uploaded on failure. A successful workflow confirms cross-compilation and packaging; physical USB upgrade, NAND boot, panel, audio and iPhone acceptance require a separate board run and are recorded as `NOT_RUN` in the source manifest.

The Demo D213 AIC8800D40 firmware matrix builds hardware and software MFi variants using `RTTHREAD_FIRMWARE_BSP_REF` and `RT_CARPLAY_REF` from `.ci/dependencies.env`. CI checks for the SDK authentication header before running coupled Demo jobs. Update the published SDK and integration pins together to enable those jobs. Locally, place the matching SDK and Demo in their submodule directories of a disposable integration checkout and run one command per checkout.

```sh
bash tools/build_d213_mfi.sh /path/to/isolated/luban-lite-hardware hardware
bash tools/build_d213_mfi.sh /path/to/isolated/luban-lite-software software
```

The wrapper uses the integration's image and CPIO verification and checks the resulting firmware symbols against the selected backend. `output/ci/d213-demo128-nand-carplay-aic8800d40/` contains the output for that checkout. The software variant uses OTA compatibility ID `demo128-nand-carplay-aic8800d40-mfi-software`, which separates its CPIO from the hardware variant. Provision software identities independently of firmware and CI artifacts.

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

Run `make -C application/rt-thread/rtcarplay check-lubanlite-video SDK_ROOT="$PWD"` when changing the video path. This covers the actual MPP sink and host UI policy, including the SDK framebuffer command ABI. Verify first-frame handoff, local UI restoration and touch on the [physical panel](single-screen-fusion.md).

## Standalone Host validation

The Demo Host workflow uses `.ci/dependencies.env` to pin the integration checkout, rt-carplay and cherryusb-ipheth. It verifies all three commits and runs formatting, bilingual documentation checks, SDK and Demo ASan/UBSan regressions, and both LVGL renders. Initialize the BSP submodules recursively and update the three pins together after qualifying a dependency change.

From the standalone Demo checkout, point `SDK_ROOT` to the pinned integration checkout. For Host builds, `RTCP_ROOT`, `CHERRYUSB_ROOT`, `IPHETH_ROOT` and `LVGL_ROOT` override the corresponding dependency paths. Use absolute paths for overrides. For firmware, `RTCARPLAY_DIR` accepts an external rt-carplay source path and the application selects that single SDK build. Keep its Kconfig compatible with the pinned integration package.

```sh
export SDK_ROOT=/path/to/luban-lite-carplay
cmake -S tests/launcher_preview -B build/preview -DSDK_ROOT="$SDK_ROOT"
cmake --build build/preview -j8
mkdir -p build/screens
build/preview/launcher_preview build/screens
build/preview/fusion_preview build/screens
```

Firmware CI also runs the Demo OTA worker against the generated CPIO with modeled HTTP and NAND, including validation and the inactive OS/Rodata write mapping.
