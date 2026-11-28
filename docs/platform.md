[English](platform.md) | [简体中文](zh-CN/platform.md)

# Architecture and platform adapters

## Ownership and data flow

Both builds use `app/receiver_session.c` for Receiver creation, callback registration and destruction, `app/connection_retry.c` for bounded retry delays, and `app/phone_track.c` for merging Now Playing updates. Each platform owner controls when to start and retire a session, including the teardown order for borrowed media resources. The shared Phone Hub view renders copied track data, playback state and timing.

On RT-Thread, `app/connection_manager.c` arbitrates transports. The poll worker in `app/carplay_app.c` owns the Receiver, publishes copied session snapshots and consumes a command queue. Launcher and Fusion display those snapshots and enqueue user requests. Refer to [session snapshots](session-snapshots.md) and [connection management](connection-manager.md) for shutdown and handoff.

The platform sinks retain buffer ownership through asynchronous decode, presentation and VSync. Keep the bounded queues, backpressure and release paths when adapting new hardware. Reuse the SDK platform descriptors to keep audio/video buffers on their existing paths.

## Dependency audit

| Area | Current dependencies | Porting boundary |
| --- | --- | --- |
| Receiver and commands | Public `rtcp/receiver.h`, platform/media descriptors and session snapshots. Application composition and iAP2 use internal `carplay_ctrl.h`, `iap2_session.h`, `iap2_stream.h` and AirPlay plist/discovery helpers. | Move production callers to public Receiver operations as equivalent contracts become available. |
| Wired and identity | CherryUSB, USBMUX/lockdown and host hooks, native NCM and lwIP, MFi over I²C, RT-Thread DFS, mbedTLS and AIC CE TRNG. The Demo uses internal `lockdown.h` and `usbmux_host.h`. | Keep the USB Ethernet driver in cherryusb-ipheth. Keep device-specific USB, pairing persistence, authentication and entropy behind adapter boundaries. |
| Media and UI | RT-Thread devices, MPP/DE and framebuffer layer commands, touch input, LVGL; internal audio mixer, focus, voice and jitter headers. Host tests also compile selected SDK internal sources directly. | Preserve decoder frame lifetime, async VSync and audio teardown. Keep Host white-box tests pinned to the SDK revision. |
| System services | RT-Thread workers/DFS, audio/PWM/RTC, lwIP/AP services, CherryUSB ADB, and Luban-Lite A/B OTA and NAND mapping. | Adapt the existing app and SDK platform boundaries as each service is ported. Keep bootloader, partition layout and packaging with the BSP. |

## Wireless variants

Realtek uses `wifi_conf.h` and `wifi_util.h` in `platform/rtcp_wifi_lubanlite.c` and Zephyr H:5 with Classic BR/EDR, SDP, RFCOMM and persistent bonds. AIC8800D40 uses the SDK SDIO WLAN and Bluetooth firmware initialization with Zephyr H:4. The RT-Thread SoftAP path programs the Apple Vendor IE through the SDK before enabling the AP.

Both targets expose STA maintenance under **Settings → Connections → Wi-Fi** on OTA builds. The page shows whether the CarPlay SoftAP is active. Entering maintenance pauses the CarPlay connection manager and stops SoftAP before enabling STA scan and association. Leaving maintenance disconnects STA and restores the selected CarPlay connection policy. The application uses AP and STA in separate modes. On AIC8800D40, verify the Vendor IE in Beacon and Probe Response frames, pairing and bond restoration, STA scan and WPA2 association, DHCP and OTA reachability, and wireless CarPlay recovery after leaving maintenance.

AIC8800D40 has a separate hardware target with A/B OTA. Its CPIO carries a specific compatibility ID. RTL8733BS retains its legacy ID, so each target rejects the other's CPIO before NAND writes.

## Linux path

The Linux main loop owns the Receiver and invokes Launcher commands synchronously on the same thread. The Launcher receives a session snapshot with the Receiver and screen-stream generations. On endpoint replacement it clears local touch ownership without sending the old contact report to the new Receiver. libusb and ALSA keep their worker threads. USB endpoint selection takes priority when its address is ready, while RT-Thread uses its transport reservation worker.

Build the native Linux LAN Receiver with `cmake -S . -B build/linux -DRTCP_ROOT=/path/to/rt-carplay -DRTCP_MBEDTLS_DIR=/path/to/mbedtls` and `cmake --build build/linux`. `CARPLAY_USB`, `CARPLAY_BLUEZ`, `CARPLAY_ALSA`, `CARPLAY_LVGL` and `CARPLAY_AIC_MPP` are independent opt-ins. Enable LVGL with `-DLVGL_ROOT=/path/to/lvgl`, using the revision in `.ci/dependencies.env`. D21x Linux firmware enables all five options through the Luban integration. The Linux `theme` and `display-mode` files and the RT-Thread `CPUI` record keep their existing formats.

Production Launcher entry points still compose platform-specific pages and bind their platform services. The common UI includes the shell, Dashboard, Phone Hub and Fusion strip. The RT-Thread preview exercises its production Launcher and the common UI widgets.
