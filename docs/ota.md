[English](ota.md) | [简体中文](zh-CN/ota.md)

# Firmware updates

Use **Settings → System update → Firmware URL** or `carplay_ota source <img-url>`. Enter the published Nightly asset URL or an [internal mirror URL](ota-mirror.md) pointing directly to a D213 CarPlay `.img`. For Wi-Fi, enter STA maintenance to download over the [selected WAN](network-manager.md). Exit STA maintenance and disconnect CarPlay before installing.

1. Use **Download image** or `carplay_ota download`. **Cancel download** stops at the next transfer boundary. For HTTPS, set or synchronize the RTC first so WebClient can validate the certificate. HTTP works with an unset clock.
2. When **Ready to install** appears, select **Install update** and confirm the displayed version. In MSH, `carplay_ota status` prints the confirmation command `carplay_ota install <candidate>`.
3. Saving a new URL or restarting discards the staged image. Enter a new direct URL if the asset filename changes. Old manifest URLs are ignored on migration.

The worker stages up to 32 MiB subject to free space in `/data`. It checks the ArtInChip container bounds, board and NAND layout, required components, and embedded CRC32 values. Before any erase, it checks the staged image again against the metadata verified during download. It writes and reads back only OS and Rodata in the inactive pair, then commits the trial boot through the SDK. Installation works offline. The checksums detect image corruption. HTTPS authenticates the server when used.

## A/B baseline and recovery

Flash the complete IMG containing `carplay_ab_baseline=2` and its matching Bootloader before installing with this service. OTA updates inactive OS and Rodata and preserves `/data`. Full USB installation includes a data filesystem, so back up settings before migrating.

The updater saves the previously confirmed slot when committing a trial. The Bootloader counts trial attempts and restores both OS and Rodata once the limit is reached. GUI confirmation requires the launcher timer, input and Connection Manager startup, both filesystem mounts, and matching active and next slots. Confirmation works offline and without a phone. A pending trial blocks a new install. The boot-limit result is retained in the environment. Watchdog-triggered resets on a hung trial and vehicle-state gating still require integration.

Physical validation is **NOT_RUN**. Record the IMG hash, Bootloader baseline and slot. Test download cancellation, power interruption during download, inactive erase/write and environment commit, healthy A→B→A, and failed trials. Check OS/Rodata rollback together and preservation of settings and pairing. Use the application updater for normal updates and keep the complete IMG for recovery.
