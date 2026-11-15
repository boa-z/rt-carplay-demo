[English](launcher-network-verification.md) | [简体中文](zh-CN/launcher-network-verification.md)

# Launcher Ethernet verification

## Source and behavior

This local delivery extends Launcher V2 on the existing `carplay-oss` branch, based on `b933ef756afd6c9498d99d016482693b45f048b0` plus the uncommitted Internet-sharing implementation. The base commit alone does not identify the image. No branch, pull request, push or remote operation was performed. See [Launcher V2](launcher-v2.md) for operation and [Internet sharing](internet-sharing.md) for routing and protocol limits. The earlier [Internet verification](internet-verification.md) identifies the previous images; this report identifies their Launcher enhancement.

The home Ethernet shortcut opens an actual LVGL network page with adapter/link/address/gateway/DNS, the configured HTTP 204 target, check progress/result/age, a nonblocking Check now action and independent sharing state. WAN evidence is now independent of AP availability. WAN changes invalidate it; AP changes affect forwarding only. The existing single network worker handles automatic and manual checks. No portable SDK, audio, video or CarPlay protocol behavior was changed.

## Executed software verification

| Check | Result | Evidence and limits |
| --- | --- | --- |
| Full Host regression | PASS | Application Makefile check under ASan/UBSan, using BUILD=build-network-validation; actual connection, media, input, USB, radio/bond and recovery code with modeled device boundaries. A fresh build directory avoids the copied CMake cache's original absolute source path. |
| Production lwIP routing and actual supervisor | PASS | Two CTest executables under ASan/UBSan; includes independent WAN success without AP, forwarding restoration, copied network status, coalesced manual requests, stale results, link/address changes, HTTP failures and evidence expiry. RT-Thread and socket boundaries are modeled. |
| Real LVGL renderer and events | PASS | Twelve 1024×600 previews; unavailable, unplugged, checking, online and failed network pages; missing gateway, expiry, button disable/request routing, Back and video takeover; existing settings/device/audio/pairing interactions. Inputs are modeled, not board measurements. Home and online page PNGs were visually inspected. |
| GUI complete image | PASS | Existing GUI defconfig with Ethernet/sharing enabled, matching cached same-source Bootloader, linked network code, nine IMG components with bounds/CRC verification. |
| Headless complete image | PASS | Existing Headless defconfig with sharing disabled, matching Bootloader, absent NAPT symbols, nine IMG components with bounds/CRC verification. Binary matches the previous Headless delivery. |
| Documentation and static checks | PASS | Paired documentation headings/code/literals/links/prose, SConscript AST, workflow matrix and git diff whitespace. |
| Board and iPhone acceptance | NOT_RUN | No flashing or hardware test performed. Ethernet DHCP/PHY, manual check, cable/outage recovery, touch response and simultaneous CarPlay load remain for board verification. iPhone gateway adoption requires independent routing evidence. |
| Remote CI | NOT_RUN | Local builds/tests only. |

## Reproducible commands

```sh
make -C application/rt-thread/rtcarplay BUILD=build-network-validation -j8 check
cmake -S application/rt-thread/rtcarplay/tests/internet_share -B build/network-check -DCMAKE_BUILD_TYPE=Debug
cmake --build build/network-check -j8
ctest --test-dir build/network-check --output-on-failure
cmake -S application/rt-thread/rtcarplay/tests/launcher_preview -B build/launcher-preview
cmake --build build/launcher-preview -j8
mkdir -p output/launcher-network-preview
build/launcher-preview/launcher_preview output/launcher-network-preview
```

## Artifact identity

Artifacts are in `output/launcher-network-validation/`. The gui and headless directories include complete IMG, firmware.config, bootloader.bin, image-layout.json, firmware-symbols.txt and SHA256SUMS. The delivery also contains preview PNG/PPM files, raw logs, a modeled lwIP routing capture, changed-source.zip, local-changes.patch, source-manifest.json and ARTIFACT-SHA256SUMS. The manifest records every changed source file's hash plus submodule identities. The SDK and IPHETH submodules remain clean and unchanged. Builds ran sequentially in the byte-matched WSL ext4 copy to avoid Windows filesystem build overhead; the designated checkout retains its GUI configuration.

| Variant | IMG SHA256 |
| --- | --- |
| GUI | `9724728af97e932aef1e243aeae881f856c57b080410bdb028acb52c43a5770d` |
| Headless | `1335f773c7e8716b98294f1eb27f82647f26d39ad5d110a4d0a90282514c24c3` |

The default connectivity target is configurable. A failed check may reflect a blocked target, DNS, a timeout or captive portal; it is not evidence that every Internet destination is unreachable. The check does not establish iPhone Internet access. On the board, first test without a phone: boot, open the Ethernet shortcut, obtain a DHCP lease, press Check now, unplug/replug the cable and observe old success clear and a new result appear. Then repeat during wireless and wired CarPlay, recording this image hash and raw logs.
