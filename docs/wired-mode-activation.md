# Wired USB mode activation investigation

[中文](zh-CN/wired-mode-activation.md)

## Failure boundary and historical comparison

The supplied failing logs complete USBMUX pairing, CarKit TLS, MFi authentication and identification. Apple Ethernet remains at zero received frames; its first OUT transfers time out with zero actual bytes. Effective link and locally valid IPv6 do not establish communication with the phone. This failure precedes AirPlay and cannot be attributed to rendering or music decoding from these logs.

The last user-confirmed startup/picture image is `output/wired-invite-recovery/gui/d213-carplay-gui.img`, SHA256 `a1efd44e3e714d7a9ab106a2263284105c1f3994726e66924eb24083ed0e2b51`. Its frozen functional C/header/patch/defconfig/SConscript overlay matches parent `9da899012eb5f180e35819201eedd96b392e5a87` and IPHETH `ed797d2a89b0e4b060b9f9e650f8356ded852609` after line-ending normalization. That image subsequently exhibited a music crash and stutter; it is a startup comparison, not a stable release. Its success log already has Ethernet RX/TX before MFi completes, but omits initial USB enumeration, so it cannot establish the successful USB personality.

| Change after the startup baseline | What it changes | What the available evidence establishes |
| --- | --- | --- |
| `7784b996` | Safe EHCI asynchronous descriptor reclamation | Both later safe IRQ-order variants fail; this does not compare against the original unsafe implementation or isolate a bad commit. |
| `6e950099` | Independent receive locking and queued lwIP input | Changes scheduling and lifetime; it does not change Apple MAC/NCM request encoding. |
| `fa9439c0`, `50cca15d` | Host/session readiness policy and network timer budget | Can unblock local DAD but observed zero-byte OUT timeouts remain; local readiness is not phone readiness. |
| `a0ef1ec9` | Attached-phone retries and USB file sync composition | Retains the earlier configuration-selection shortcut. |

The SDK remains at `894575c161ffcfaa5862a06858bb254bf2c03776`. No single regression commit has been proven. Reverting memory-safety fixes is not justified by this comparison.

## Application policy correction

Previously the application returned immediately when any USBMUX plus Apple Ethernet configuration existed, skipping vendor request `0xc0/0x52`, value zero, index four, one response byte. Ordinary tethering descriptors therefore suppressed a potentially necessary mode transition. This shortcut also exists in the last startup baseline: it is a reproducible latent policy defect, not proof of a newly introduced regression.

Community reference [DiPlay at 615b32ce](https://github.com/shihabal3amri/DiPlay/tree/615b32ce0b40cd0b99ce94ba23a512001553684f), particularly `IphoneCarPlayConfiguration.kt`, `IphoneUsbHost.kt`, `WiredConfigurationAction.kt` and `CarPlayController.kt`, distinguishes usable Ethernet from an observed USBMUX + CDC-NCM + Apple Ethernet composite personality. This is community interoperability evidence, not an official Apple specification. No reference source code was copied.

The application now prefers that observed composite personality. Otherwise it sends the mode request once before class binding and rereads the device/configuration descriptors within the existing bounded five-second window. Configuration indexes and values come from descriptors. A rejected or short request retains an available compatible fallback; an accepted request with unchanged descriptors uses a freshly read fallback after the window. Repeated device-descriptor failure discards stale fallback selection. CDC-NCM-only configurations are excluded when the corresponding class driver is not built. A one-byte reply only establishes USB request completion, not usable Ethernet or AirPlay acceptance.

This change stays in the application USB port. It does not change the SDK, IPHETH, EHCI, lwIP, media pipelines or concurrent OTA work. A phone that retains full composite descriptors after an ended session may still require another activation request; the current initial-enumeration heuristic does not establish or solve that case.

## Verification and next board capture

The Host regression compiles the actual application port with synthetic descriptors and a modeled phone transition. The old implementation fails the selection assertion by choosing ordinary Ethernet without issuing activation. ASan/UBSan checks pass for activation with refreshed descriptors, an already-present composite mode, rejected requests, short replies, unchanged descriptors and a device that stops responding. Existing DMA/cancellation and boot-attached owner checks also pass. These fixtures demonstrate the application decision, not actual iOS behavior; their descriptor counts and indexes are not required phone values.

Run `make -C application/rt-thread/rtcarplay check-lubanlite-usb BUILD=/tmp/carplay-usb-check` on a Linux/WSL host. The dedicated GUI image, frozen source manifest, build logs and verification report are in `output/wired-regression-deep/`. No Headless firmware is generated. The snapshot preserves concurrent uncommitted OTA/UI work and is not a commit-only build.

Board acceptance remains NOT_RUN: the AiBurn service at `127.0.0.1:9100` refused connection during this investigation. Capture the complete boot/enumeration log, including mode-request byte count, refreshed descriptors and selected index, MAC/NCM setup, RX/TX before MFi, IPv6, StartSession, inbound RTSP and first picture. Keep iPhone on USB1 Host. Check both phone-connected-at-boot and unplug/replug, followed by an ended-session retry without unplugging. If the corrected selection still produces zero-byte OUT timeouts, retain its pre-cancellation EHCI snapshot and compare against the exact startup baseline under the same phone state. Music crash, stutter, wireless regression and PC file-sync acceptance still require separate board checks.
