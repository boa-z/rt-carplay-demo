[English](video-handoff-fix.md) | [简体中文](zh-CN/video-handoff-fix.md)

# CarPlay connects but the launcher covers its video

## Reported firmware and failure

The user flashed `d213-carplay-gui-38217874945d6a3035293cc6831257b53d67bd92` (IMG SHA256 `cea07af4ec8a17bd2e610dee45e45d77c295eca0e4aa749e4ce4d059b1ffa20a`). Boot and CarPlay connection succeeded, but video did not display correctly. Boot success does not establish video acceptance.

## Confirmed source and binary defect

`ui/video_layer_policy.c` included `<sys/ioctl.h>` without first including `<aic_common.h>`. RT-Thread's libc ioctl macros encode direction and structure size. The SDK driver `bsp/artinchip/drv/display/drv_fb.c` includes `aic_core.h`, which loads `aic_common.h` and defines commands using only the type and command number. Both headers guard existing macros, so include order determines the ABI.

| Operation | Faulty UI command | SDK driver command |
| --- | --- | --- |
| Read UI alpha | `0xc0104626` | `0x4626` |
| Write UI alpha | `0x40104627` | `0x4627` |

RISC-V disassembly of the pre-fix UI object contains the faulty constants; the built driver's `aicfb_ioctl_cmds` table contains the SDK constants. The driver rejects the UI query as an invalid command. The policy consequently keeps the opaque launcher visible above the DE video layer. The SDK's video frame count can increase independently of UI visibility, so the connection page can report a live session while covering the actual video.

## Correction and regression coverage

Load `<aic_common.h>` before the policy's RT-Thread/libc headers, matching the driver and the existing MPP sink. Remove the policy's direct `<sys/ioctl.h>` dependency. Preserve capture of the previous alpha setting and exact restoration on disconnect; the host still owns UI visibility. Log alpha-query failures and successful video handoff.

The Host regression checks the driver's numeric commands independently of the caller's macros. Its framebuffer model rejects other command values. The SDK test header now models the guarded SDK command macros instead of silently relying on Linux ioctl encoding. The regression failed before the correction at the `0x4626` assertion; after correction the actual policy, alpha restoration and MPP sink regressions pass with ASan/UBSan. Firmware CI executes this regression before building both GUI and Headless images.

## Board acceptance still required

The source/binary ABI mismatch is confirmed. Whether it explains every symptom on the connected board requires the corrected IMG and board logs. This task found no `aiburnconfig.json` in the CarPlay checkouts and has not read the board's serial console or flashed a replacement image. New builds remain **NOT_RUN** for physical video acceptance until tested on the board.

On the corrected GUI image, connect the phone, verify an actual moving CarPlay picture and touch input, then disconnect and verify that the launcher returns. Repeat reconnect and USB/wireless mode switching as applicable. The console should show `[carplay-ui] video visible, UI alpha=0`; `carplay_video status` should show decoded frames without persistent decode errors. If display is still abnormal, preserve the full serial log plus `carplay_status` and `carplay_video status`; do not infer successful display solely from the connection state or decoded-frame count. Audio behavior is unchanged.
