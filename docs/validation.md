[English](validation.md) | [简体中文](zh-CN/validation.md)

# Hardware and validation

| Target | Implemented scope | Host | Cross-build | Physical |
| --- | --- | --- | --- | --- |
| D213, RTL8733BS, 128 MiB NAND | RT-Thread CarPlay, Launcher, Fusion and BSP services | Demo Actions suite | D213 firmware Actions | `NOT_RUN` for this migration |
| D213, AIC8800 | Planned. SKU and module integration pending. | `NOT_RUN` | `NOT_RUN` | `NOT_RUN` |
| Embedded Linux | LVGL Preview and modeled adapters. Production Receiver planned. | Launcher and Fusion renders | `NOT_RUN` | `NOT_RUN` |

See the [CI guide](ci.md) for runnable checks. A green Host or cross-build result records software validation for that commit. Record board results with the source manifest, board revision, WLAN module, iPhone and iOS versions, and serial logs with identifiers removed.

## D213 board acceptance

- Cold and warm start. Check Launcher, Settings, theme and language changes.
- Begin with clean test pairing storage. Verify wired trust and MFi authentication, wireless BR/EDR pairing, persistence and automatic connection. Switch between wired and wireless, then disconnect and reconnect each transport.
- Exercise 30 FPS and 60 FPS video, first-frame handoff, VSync and queue backpressure, Fusion, fullscreen, local display recovery, and single and multi-touch across display modes.
- Verify music, calls, Siri, microphone and audio recovery during interruptions and after returning to the Launcher.
- Verify Now Playing and artwork, route guidance and supported vehicle/GNSS status in the intended bench setup.
- Verify network sharing, Wi-Fi STA and coexistence, and USB file synchronization.
- Verify recovery IMG, then update the inactive OS/Rodata pair with A/B OTA and confirm `/data` retention, reboot and rollback behavior.
