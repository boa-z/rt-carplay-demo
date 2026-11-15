[English](wired-irq-ab.md) | [简体中文](zh-CN/wired-irq-ab.md)

# Wired startup: controlled EHCI image comparison

Board follow-up: the user's `1.txt` (candidate) and `2.txt` (control) both fail before first RX with repeated `03 03 03 00`. Both later report timeout-pool exhaustion on wireless recovery. This experiment did not restore connection; see the [session link follow-up](wired-session-link.md). The procedure below documents the original experiment, not a renewed recommendation to retest these images.

## Purpose and evidence

The accepted startup image uses parent `9da89901` and IPHETH `ed797d2`. Its log already has Ethernet RX/TX before MFi completion. The later failure has one armed Bulk IN, no RX completion and a four-byte Carrier response decoded as down. The regression range includes EHCI interrupt retirement order and later RX/lwIP scheduling changes. This experiment isolates the interrupt-order candidate; it does not assert an established hardware root cause.

## Firmware variants

Artifacts are under `output/wired-irq-epoch/`. `control/gui/` retains the previous IAA-before-scan algorithm. `candidate/gui/` snapshots the old IAA cohort, scans completions first, then retires only the old cohort. `candidate/headless/` checks the same candidate with the Headless configuration. The two GUI images share the same frozen application, SDK, driver and configuration; their behavioral difference is confined to the EHCI handler. A diagnostic policy string also differs. The candidate does not restore the unsafe original loop that can retire a new removal using an old IAA.

Network status identifies the image as `[wired-test] EHCI completion=iaa-before-scan` or `[wired-test] EHCI completion=epoch-after-scan`. It also captures up to eight Carrier-status bytes, RX counters, IPv6 state and bounded read-only EHCI snapshots before cleanup. The build bundle contains the effective generated HCD source, patch, configuration, ELF, raw firmware, packaged FIT payload and hashes for each image.

The build runs in a dedicated native source copy. It never restores, resets or writes configuration back to the shared Windows checkout. Concurrent OTA work is outside this experiment; changes made after the source freeze are not automatically included in its images or validated by its results. No branch change or PR is needed.

## Board procedure

1. Use `candidate/gui/d213-carplay-gui.img` first. Keep the same phone, USB port, cable and Ethernet connection. Capture the complete cold-boot log through connection/picture or the first failure and cleanup; allow at least 60 seconds for the capture. Do not delete trust records as part of this comparison.
2. Confirm the `epoch-after-scan` marker. Record whether RX completions appear, IPv6 becomes valid, stage 4/5 is reached and a picture appears. Keep initial testing focused on startup; music stability and smoothness are separate acceptance checks.
3. If the candidate connects, repeat a cold start and USB reconnect, then compare `control/gui/d213-carplay-gui.img` under the same conditions. Confirm its `iaa-before-scan` marker and repeat the same observation. Three runs per variant help distinguish a consistent difference from intermittent startup.
4. Preserve image SHA256 and full logs with each result. Existing `ipheth_status` and `usb_ehci_status 1` remain available for additional samples; automatic samples already run before network timeout cleanup.

| Observation | Interpretation |
| --- | --- |
| Candidate consistently connects; control consistently fails | Supports EHCI scan/callback order as a regression contributor; retain logs before claiming the complete root cause. |
| Both fail before first RX | This ordering change is insufficient; use raw Carrier replies and descriptor progress to investigate device activation or receive completion. |
| Both connect, or results vary | A deterministic bad commit has not been isolated; compare cold-start/reconnect conditions and attachment state. |
| Candidate fails; control connects | The candidate is not accepted and its behavior needs investigation. |

## Software validation boundary

Actual HCD tests cover IAA plus INT/ERR, old/new cohorts, an empty old cohort, scan-before-callback sequencing, short/zero/full/error transfers, control status and cache handling. A negative control without the cohort guard fails as expected. GUI/Headless builds and IMG bounds/CRC checks are software evidence. Wired startup, sustained music, touch, wireless regression and OTA operation require their own board evidence. No connection or performance improvement is claimed before those tests.
