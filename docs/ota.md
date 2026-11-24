[English](ota.md) | [简体中文](zh-CN/ota.md)

# Firmware updates

Use **Settings → System update → Firmware URL** or `carplay_ota source <cpio-url>`. Enter the Nightly `ota.cpio` URL or an [internal mirror URL](ota-mirror.md). For Wi-Fi, enter STA maintenance to download over the [selected WAN](network-manager.md). Exit STA maintenance and disconnect CarPlay before installing.

1. Use **Download update** or `carplay_ota download`. **Cancel download** stops at the next transfer boundary. For HTTPS, set or synchronize the RTC first so WebClient can validate the certificate. HTTP works with an unset clock.
2. When **Ready to install** appears, select **Install update** and confirm the displayed version. In MSH, `carplay_ota status` prints the confirmation command `carplay_ota install <candidate>`.
3. Saving a new URL or restarting discards the staged package. After migrating from the IMG updater, save the `.cpio` URL once.

The worker stages up to 32 MiB subject to free space in `/data`. It checks the target board, CPIO member names, partition mapping, bounds and checksums before offering installation. It repeats those checks before any erase. The Luban-Lite OTA downloader unpacks the archive, writes OS and Rodata to the inactive slots and verifies the readback. The SDK then commits the paired trial boot. Installation works offline.

## A/B baseline and recovery

Devices with A/B baseline 2 and the previous IMG updater can install the new Nightly IMG through their existing updater once, then switch to `ota.cpio`. To establish A/B baseline 2 or recover a device, flash the complete IMG over USB. Full USB installation includes a data filesystem, so back up settings first. CPIO updates preserve `/data`.

The updater saves the previously confirmed slot when committing a trial. The Bootloader counts trial attempts and restores both OS and Rodata once the limit is reached. GUI confirmation requires the launcher timer, input and Connection Manager startup, both filesystem mounts, and matching active and next slots. Confirmation works offline and without a phone. A pending trial blocks a new install. The boot-limit result is retained in the environment. Watchdog-triggered resets on a hung trial and vehicle-state gating still require integration.

Physical validation is **NOT_RUN**. Record the IMG hash, Bootloader baseline and slot. Test download cancellation, power interruption during download, inactive erase/write and environment commit, healthy A→B→A, and failed trials. Check OS/Rodata rollback together and preservation of settings and pairing. Use the application updater for normal updates and keep the complete IMG for recovery.
