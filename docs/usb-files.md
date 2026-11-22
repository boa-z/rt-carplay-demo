[English](usb-files.md) | [简体中文](zh-CN/usb-files.md)

# USB development file transfer

`RTCARPLAY_USB_FILE_SYNC` composes CherryUSB's existing ADB class, sync service, demo descriptors and RT-Thread DFS bridge in the CarPlay application. It does not enable the separate RT-Thread adbd package or a shell service. The PC still uses Android Platform Tools `adb push` and `adb pull`. CherryUSB and rt-carplay upstream sources, the RTOS and SoC drivers are unchanged by this file-transfer integration.

The GUI development configuration assigns USB0 to device mode for the PC and leaves USB1 as the iPhone host. The sole host controller is now numbered bus 0; physical USB1 is not host bus 1. Use `usb_ehci_status` without a bus argument to inspect all initialized hosts. USB0 can no longer host a storage device in this configuration. Disable this option and restore USB0 host mode if that role is required.

The unmodified upstream demo advertises development VID/PID `FFFF:FFFF`, a fixed demo serial and a WinUSB descriptor. These are prototype identities; a production product needs its own valid descriptors. PC driver association and adb enumeration still require board testing. This service exposes DFS paths without ADB authentication; enable it only on a controlled development board. File access goes through DFS while the board retains filesystem ownership. Do not export the same mounted volume as USB mass storage. MTP is not selected: this checkout's template requires a separately licensed commercial implementation.

## Build and checks

The application SConscript compiles upstream sources directly. The only compatibility transformation is a generated copy of `rt_vfs_dfs.c`: this toolchain defines `st_mtime` as `st_mtim.tv_sec`, conflicting with the upstream `vfs_stat` field. `tools/prepare_usb_vfs.py` preserves the vendor license and adapts those two uses, rejecting unexpected source changes. It does not edit vendor files, system headers or SDK build scripts. Generated files are ignored.

```sh
make -C application/rt-thread/rtcarplay check-usb-file-sync
```

The ASan/UBSan harness feeds the actual upstream service fragmented headers and payloads and checks a 64 KiB binary push/pull, missing/read-only errors and service close/reopen during an incomplete upload. Its VFS models a mounted volume. It does not validate USB resets, DMA, actual DFS storage, PC drivers or simultaneous CarPlay operation. USB reset may retain an incomplete file handle until the next sync open resets the upstream service. Interrupted uploads are not atomic and may leave partial files; transfer to a temporary name and validate before applying live configuration.

## Board procedure (not yet executed)

Connect the PC to USB0 and the iPhone to USB1. Confirm the `[usb-files]` boot marker. Use a unique test file on the confirmed writable TF mount `/sdcard`, or another confirmed writable DFS mount:

```sh
adb devices
adb push check.bin /sdcard/check.bin
adb pull /sdcard/check.bin check-return.bin
```

Compare SHA256 of both PC files. Repeat with empty and multi-megabyte files, disconnect/reconnect USB0 during transfer and verify a subsequent transfer succeeds. Repeat while CarPlay shows video and plays music. Use the serial shell for file cleanup and configuration activation; `adb shell` is not linked. DFS root `/` is a namespace, not an access restriction to `/sdcard`. Do not overwrite active pairing, OTA or application configuration just to test transport.

GUI image construction and Host checks passed in this iteration. PC enumeration, actual board file I/O, USB interruption recovery and simultaneous iPhone operation are **NOT_RUN**. The existing wired CarPlay zero-RX/TX-timeout failure remains open; file transfer does not establish its repair.
