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

## Classic accessory authentication

The application selects one `rtcp_mfi_provider_t` for USB iAP2, Bluetooth RFCOMM iAP2, the Wi-Fi tunnel and AirPlay MFiSAP. Linux owns it until the USB worker, Receiver and wireless services have stopped. The RT-Thread product retains the selected provider across Receiver restarts and serializes coprocessor transactions. The hardware adapter retries a failed probe when authentication next needs the chip.

| Backend | Linux | RT-Thread |
| --- | --- | --- |
| MFi 2.0C / 3.0 coprocessor | The default for USB and wireless. Select the bus with `--i2c DEVICE`. | `RTCARPLAY_MFI`, with board settings `RTCARPLAY_MFI_I2C_BUS` and `RTCARPLAY_MFI_I2C_ADDR`. |
| Authorized software identity | `--mfi-mode software` with certificate, key and protocol. `--mfi-mode auto` uses a configured identity or selects hardware when no identity was configured. | `LPKG_RT_CARPLAY_MFI_SOFTWARE` and `RTCARPLAY_DEMO_MFI_SOFTWARE`, with a product provisioning hook. |
| External device/service | SDK integrators can supply an instance through `rtcp_mfi_provider_t`. | `RTCARPLAY_DEMO_MFI_EXTERNAL` with a product hook. |
| Synthetic identity | Native CTest generates and removes its own identity. | SDK Host tests exercise the Provider contract independently of a BSP. |

The accessory certificate and signer are provisioned separately from the local AirPlay Ed25519 receiver identity, AirPlay Pair-Setup records, USB Lockdown trust records and BlueZ or Zephyr bonds. Keep the pairing files and Bluetooth bonds across reboot or compatible updates. The Demo reports the backend and protocol version at Linux startup. A `MFi ... provider ready` log confirms local key and certificate initialization. Live iPhone acceptance must be checked separately.

### Linux setup

Enable `RTCP_MFI_SOFTWARE` in the SDK CMake build. To ship a software-only application, set `CARPLAY_MFI_HARDWARE=OFF`. Provide an authorized Classic accessory certificate as DER certificates-only PKCS#7 or a DER X.509 leaf accepted by the peer. The leaf must match the supplied unencrypted DER key. Use `2` for RSA/SHA-1 or `3` for P-256/SHA-256, per the provisioned identity. DiPlay's local Classic configuration uses protocol `3`, a P-256 PKCS#8 DER `identity.pk8` and DER PKCS#7 `certificate.p7b`. The Linux key file must be a regular file owned by the service user, with no group or other permissions. Use mode `0600` or `0400`. Configure all three options together.

```sh
build/linux/rt-carplay-demo --headless --lan eth0 \
  --state-dir /var/lib/carplay \
  --mfi-mode software --mfi-protocol 3 \
  --mfi-cert /var/lib/carplay-mfi/certificate.p7b \
  --mfi-key /var/lib/carplay-mfi/identity.pk8
```

Use `--wired` when libusb is enabled. To enable wireless alongside wired, build with `CARPLAY_BLUEZ=ON` and pass `--wired --wireless wlan0 --ap-config FILE` with the board's configured AP, DHCP and BlueZ services. The same MFi flags apply. Plain `--lan IFACE` keeps the existing LAN development flow and answers `/auth-setup` with 503 when the peer asks for MFiSAP. Missing or unusable explicitly configured credentials keep CarPlay offline. The graphical app can continue showing its local Launcher while retrying startup.

### RT-Thread setup

Build the D213 hardware baseline from the pinned Luban-Lite BSP and matching SDK. For a second firmware variant, use a separate integration tree with the same D213 defconfig, then enable both software symbols in Kconfig and rebuild. These symbols remove the Demo coprocessor backend. The product implements `carplay_product_mfi_load()` and `carplay_product_mfi_release()` declared in `platform/carplay_mfi_provider.h`. Supply the certificate and key through memory buffers from protected, provisioned storage. The D213 reference `/data` volume uses FatFS without per-user access control and the development USB file service can expose files under that mount. The reference load hook returns `RTCP_ENOENT` until the product supplies protected storage. Release and wipe the temporary key buffer after the SDK parses it. Signing uses the board TRNG. To integrate an external signer instead, select `RTCARPLAY_DEMO_MFI_EXTERNAL` and implement `carplay_product_mfi_external()` with the SDK callbacks for both iAP2 and SAP. A missing hook fails closed. The HMI and its local settings remain available.

### Validation

Use the native Linux CMake build with `-DCARPLAY_BUILD_TESTS=ON -DRTCP_MFI_SOFTWARE=ON` and run `ctest --test-dir build/linux --output-on-failure`. The test needs Python 3 and the OpenSSL CLI. It exercises a loopback RTSP request, software and auto selection, missing hardware, malformed configuration, key permissions and key mismatch. The test certificates are generated inside the local test run. Run `make check` with the matching SDK for the modeled RT-Thread USB, wireless and Receiver owner regressions. SDK and Demo revisions for these tests should be updated together in `.ci/dependencies.env` when the SDK revision is published.

On D213 or a Linux PC with compatible USB and Bluetooth hardware, use an iPhone and the authorized identity for the selected variant. First connect by USB and approve trust. Unplug and reconnect, then restart the receiver and verify that the same pairing records work without a new trust prompt. Establish a wireless connection and repeat the restart; then connect a USB cable during wireless projection and verify the configured transport policy. Exercise H.264 video, media audio, Siri and microphone, calls, touch, Now Playing, navigation and vehicle state on each working transport. Record the board, firmware and SDK revisions, mode, iOS version and failure stage. Keep private keys and device credentials out of captured logs and CI artifacts. Host tests and target compilation leave iPhone interoperability and performance to the board run.
