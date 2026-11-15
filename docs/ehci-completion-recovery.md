[English](ehci-completion-recovery.md) | [简体中文](zh-CN/ehci-completion-recovery.md)

# EHCI completion recovery

## Board evidence

The user confirmed a load-access fault during wired music playback with the delivered wired-invite-recovery/gui/d213-carplay-gui.img (SHA256 a1efd44e3e714d7a9ab106a2263284105c1f3994726e66924eb24083ed0e2b51). The trap reports mcause=5, mepc=0x40069a22, mtval=0x0000006f216000d8 and ra=0x4006b722. Its symbols place the PC in ehci_urb_waitup and the return address in USBH_IRQHandler. However, the IMG instruction at the PC is a branch, not a load. This discrepancy remains unexplained and prevents exact instruction-level root-cause attribution. Audio stack telemetry shows 1876 of 98304 bytes used at its observed peak; this does not establish audio stack exhaustion. Wired display stutter also remains a reported problem.

## Reproduced software defect

EHCI async-advance acknowledgement (IAA) belongs to queue removals made before that acknowledgement. The existing handler scans INT/ERR completion bits first, which can unlink another completed QH and request a new async advance, then uses an IAA sampled at entry to release every pending QH. With simultaneous completion and old IAA, the newly unlinked QH is released without waiting for its own acknowledgement. DMA descriptor reuse and callback delivery can therefore occur too early. This is a reproduced lifetime defect; it is not yet proven to be the sole cause of the board crash or stutter.

The local build patch processes the sampled IAA before scanning INT/ERR. New removals made by those scans remain pending for a subsequent acknowledgement. Descriptor cache handling, the existing short-transfer repair, protocol capabilities, SDK interfaces and wireless policy remain as before. The upstream CherryUSB checkout is untouched.

## Regression and verification scope

The Host regression executes the actual patched USBH_IRQHandler with an old pending QH and a second completed transfer. It combines IAA with INT, ERR and INT plus ERR, requires only the old transfer to complete in that interrupt, then requires a later IAA to release the new transfer exactly once. Before the fix, the INT case fails the callback-count assertion. After the fix, all three combinations pass under ASan/UBSan, alongside the existing short, zero-length, full, error, control-status and cache-coherency tests. The negative-test log is retained locally with the new image verification artifacts.

```sh
bash packages/third-party/cherryusb-luban/tests/run.sh
bash packages/third-party/cherryusb-luban/tests/run_control_probe.sh
```

Host results cannot establish physical USB controller cache behavior, playback stability or smoothness. New GUI/Headless build results and exact image identities belong in the accompanying artifact verification report. Board follow-up must repeat wired music and projection, test disconnect/reconnect and USB/Wireless switching, and verify wireless audio/video regression. Existing lwIP timer-pool exhaustion, audio timestamp gaps and physical echo acceptance remain separate open items. No new-image board acceptance is claimed.
