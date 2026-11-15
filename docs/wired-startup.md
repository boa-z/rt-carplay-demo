[English](wired-startup.md) | [简体中文](zh-CN/wired-startup.md)

# Wired startup and pairing performance

Latest follow-up: both controlled IRQ-order images failed with status `03 03 03 00` and no first RX. See [USB session link policy](wired-session-link.md) for the separate application-owned transport policy, timeout-budget repair and pending board checks. Earlier observations below remain historical evidence.

## Observed boundary

The supplied board log reports successful RSA pair-record generation in 6933 ms, followed by successful Pair, StartSession and CarKit StartService. It stops at stage 3, the application's network wait. This establishes successful trust/TLS setup; it does not establish IPHETH readiness, valid IPv6, iAP2 identification, MFi authentication or video. Earlier logs timed out at the same network stage. The excerpt does not prove which underlying network condition is absent.

The subsequent board test of GUI image `bb2f254e6b33984cf71dc3135383c0e7f7c30c0611c3171d07918d887c6f76b6` reaches stage 4: MFi succeeds, identification is accepted, subscriptions and CarPlayStartSession are sent for `[FE80::DCB5:4FFF:FE73:DD03]:7000`. That earlier test has no AirPlay TCP/RTSP evidence or first frame. Suspended threads and zero TX errors do not establish a deadlock or successful peer connectivity; the RX/TX totals and post-invitation snapshot are missing.

On 2026-11-14, after delivery of the invitation-recovery image, the user confirms wired CarPlay works. The supplied log records initial invitation ACK, no TCP at the five-second snapshot, one startup retry and ACK, then RTSP connection, successful pairing/verification and authentication, event connection, screen connection, first H.264 frame, visible video and stage 5. This confirms startup and picture recovery in this run. The temporal sequence supports the bounded retry's usefulness; it does not establish why the phone ignored the first invitation. The log contains the new recovery markers but no embedded image hash, so the flashed image identity is associated with this test session rather than independently verified from firmware output.

The same log reports pair-record generation in 3732 ms and 44.1 kHz stereo PCM packets reaching 48 kHz playback. It also contains a `MEMP_SYS_TIMEOUT` pool-exhaustion assertion in the `inetshare` thread and audio timestamp gaps. Keep these as open issues; startup success is not acceptance of Internet-sharing recovery, audio quality, call echo, sustained performance, touch or repeated reconnects. The current task records these findings without changing the newly working startup path.

## Startup changes

After CarKit opens, the application starts the wired iAP2 worker immediately and waits for the network in parallel. It no longer makes reading CarKit identification/MFi traffic depend on a valid IPHETH address. The existing 15-second network wait, single-session ownership and cleanup remain bounded. An exited iAP2 worker ends that wait.

The portable SDK retains a wired CarPlayAvailability invitation if authentication, endpoint availability or transmit capacity prevents its initial send. Owner-thread advances retry the pending invitation. One successful send clears it; connection reset clears it as well. This is completion of one pending invitation, not periodic wired re-invitation. Wireless negotiation retains its existing policy. Address validation and IPv6 duplicate-address detection remain mandatory; the implementation does not manufacture carrier or mark tentative addresses valid.

USB network diagnostics run after iAP2 starts and before cleanup on failure. When the optional RT-Thread IPHETH diagnostic provider is present, logs include interface flags, IPv6 slot-zero state, RX submissions/completions, readiness, carrier and transfer errors. No registered interface is reported explicitly. These observations distinguish absent registration, missing receive completion, carrier down and an address that is not yet valid.

## Stage 4 recovery and evidence

The sent-once flag previously suppressed every subsequent wired CarPlayAvailability response. Each new availability is now answered, including deferred delivery if the endpoint temporarily disappears. Existing accepted AirPlay TCP or event connections suppress further wired invitations. Wireless invitation pacing is unchanged.

The application enables one extra wired startup invitation after `RTCARPLAY_WIRED_REINVITE_DELAY_MS` (default 10000 ms) if TCP has not been accepted. Zero disables this compatibility policy; the portable SDK defaults to disabled. The delay is an application choice, not an Apple protocol requirement, and the retry never becomes a periodic loop. DiPlay at `615b32ce0b40cd0b99ce94ba23a512001553684f` documents a late USB-network/reused-session condition and a bounded second invitation in [WiredStartAgain.kt](https://github.com/shihabal3amri/DiPlay/blob/615b32ce0b40cd0b99ce94ba23a512001553684f/shared/src/main/java/com/shilapi/xcertplay/orchestration/WiredStartAgain.kt). Its condition and NCM transport differ from this board; this reference supports investigating recovery, but does not prove the cause of the supplied IPHETH stall. No private protocol behavior is claimed as an official specification.

Automatic snapshots at 5, 15 and 30 seconds of stage 4 report receiver TCP/auth/session state and IPHETH counters. IPv6 header counters distinguish neighbor solicitation/advertisement (`NS`/`NA`), initial TCP `SYN`, `SYNACK` and `RST`, and report the last TCP ports. RX counts headers received from USB before lwIP delivery; TX counts successful USB submissions, which do not prove iPhone acceptance. IPv6 extension headers are counted only as IPv6. No audio/video or packet payload is logged. The iAP2 log distinguishes availability, initial send, one startup retry and link ACK; a link ACK does not prove application acceptance.

If the new image still stalls, collect the automatic snapshots and these read-only commands twice, five seconds apart, while USB remains attached:

```text
carplay_connection status
carplay_usb net
ipheth_status
netstat
```

An RX `SYN` to port 7000 without a TX `SYNACK` points toward listener/stack delivery; TX `SYNACK` without a completed TCP connection requires USB/peer investigation. No RX `SYN` requires examining invitation ACK, peer state and neighbor discovery first. These are diagnostic branches, not confirmed root causes.

## Pairing optimization

The selected embedded mbedTLS source is 2.28.1, despite the legacy package-selection label. Its MPI architecture selection omitted RV64 and used 32-bit limbs on the 64-bit D213 target. The selection now recognizes RV64 with GCC double-width arithmetic. Both independent RSA keys remain 2048 bits with exponent 65537; prime checking, certificates, TRNG and pair-cache formats are unchanged. The optional monotonic clock reports pair-record generation separately from the phone's trust-dialog wait. Cached pairing skips generation.

## Reproducible software checks

```sh
make -C packages/third-party/rt-carplay -o check-docs -j8 check
make -C application/rt-thread/rtcarplay BUILD=build-network-validation -j8 check
sh packages/third-party/mbedtls/tests/run-pair-mpi.sh output/pair-mpi-native
CC=riscv64-linux-gnu-gcc AR=riscv64-linux-gnu-ar RUNNER="qemu-riscv64 -L /usr/riscv64-linux-gnu" sh packages/third-party/mbedtls/tests/run-pair-mpi.sh output/pair-mpi-rv64
```

The MPI checks compile both 32-bit and automatically selected 64-bit variants, run upstream arithmetic vectors, generate a full pair record, verify certificate chains, validate private keys and exponents, check independent host/root identities, and reject a failed entropy provider. Test entropy is deterministic and must never be used in firmware. Emulator and host timings are not D213 latency measurements. The application regression models a phone whose network cannot progress before iAP2 starts; the SDK regression delays endpoint availability until after a single availability message and checks exactly one subsequent invite.

## Board acceptance

Flash the image whose checksum is recorded in its artifact report. Keep USB attached while collecting logs through identification, MFi, the wired endpoint invitation and the first displayed frame. If startup still fails, retain the automatic network snapshot and the initial IPHETH enumeration messages before cleanup. Reconnect with the saved pair record to check cache reuse, then verify detach/reconnect, wired audio/video/touch and wireless fallback. Host checks and image validation do not establish these board outcomes. The unresolved wired audio quality, video stalls and acoustic call echo require their own physical regression checks.

The subsequent board test reported a wired music-playback crash (RISC-V load access fault: mcause=5, mepc=0x40069a22, mtval=0x0000006f216000d8, ra=0x4006b722) and continuing wired UI stutter. Startup and picture acceptance therefore does not establish playback or stability acceptance. The delivered GUI symbol table places the PC in ehci_urb_waitup and the return address in USBH_IRQHandler, but the delivered IMG bytes at the PC encode a branch, not a load. The user subsequently confirmed this exact delivered GUI IMG was flashed. The disagreement between the image instruction and the trap report remains unexplained; exact instruction-level attribution and a board-validated fix are still pending. Audio stack telemetry reports 98304 total bytes and 1876 peak used bytes; this sample does not establish audio stack exhaustion. USB URB/QH lifetime, cache coherency and memory corruption remain investigation candidates, not confirmed root causes. No crash or stutter fix is claimed by this publication.

## Stage 3 with no first USB RX

A later board run reaches MFi authentication, identification and CarPlayAvailability but exits stage 3 with RTCP_ETIMEDOUT: IPHETH has submitted one RX with no completion, Carrier is down and IPv6 remains tentative. This is before video setup and cannot be classified as a decoder or frame-rate issue. The phone's silence versus a missing controller completion remains unresolved from this excerpt.

After successful Lockdown/CarKit session open, the application now publishes an optional `usbh_ipheth_host_ready` notification through its board USB adapter. The adapter captures the attachment generation before pairing and rejects stale publication after detach/rebind. This permits actual Carrier polling once RX has been submitted without inventing a first completion. Before the first RX, sends still require positive Carrier and armed RX; `rx_ready` remains observed only. Closing the session revokes the hint after the iAP2 owner has stopped. Failed owner stop retains live resources. Physical reconnect clears it. This local compatibility policy differs from Linux's default first-RX heuristic and is not asserted to be an Apple requirement or the confirmed cause of this phone's behavior. The existing 15-second network wait, IPv6 DAD, invitation pacing, single-session ownership and video pipeline remain unchanged.

The host-only regression checks silent NCM/Legacy activation, Carrier down/errors, rejected RX submission, revocation during Carrier, reconnect reset and stale attachment generations. New-board acceptance remains pending. Capture startup through first picture with `ipheth_status`, `carplay_usb net`, `carplay_connection status` and `usb_ehci_status 1` if the link still stalls. The new `Activation` line reports host-session evidence, pending RX and cumulative Carrier queries separately; do not interpret it as received data. Preserve the phone unlock/trust state and avoid deleting pair records just to retry.


## Carrier-down capture after host activation

The 2026-11-14 board log confirms a reused Lockdown session, successful CarKit TLS, the host activation marker, MFi authentication, identification and CarPlayAvailability. It still reports zero RX completions, tentative IPv6 and a successful four-byte Carrier transaction decoded as down. This disproves the sufficiency of the preceding activation change; it does not identify the raw status bytes or establish a controller fault. The supplied file ends before the network timeout and subsequent Carrier samples.

Startup diagnostics now print a bounded eight-byte Carrier reply with its captured length, actual control result and independently decoded status. Network status also collects read-only EHCI snapshots at initial startup, at 5 and 10 seconds while IPv6 remains unavailable, and before timeout cleanup. These diagnostics neither force carrier nor bypass IPv6 DAD, change the selected alternate/NCM mode, reset USB, or alter invitation/audio/video behavior. Save the complete output through the first `wired exit` and cleanup. Compare Carrier replies and endpoint 86 progress across snapshots. Active descriptors alone cannot distinguish a silent/NAKing phone from all controller faults; a USB capture may still be needed. This is a diagnostic candidate, not a board-verified connection repair.


## Regression range against the accepted image

The retained `output/wired-invite-recovery/gui/d213-carplay-gui.img` hashes to `a1efd44e3e714d7a9ab106a2263284105c1f3994726e66924eb24083ed0e2b51`. Its build-time source overlay matches the published parent `9da89901` for the connection owner and EHCI patch, and driver `ed797d2` for the CherryUSB class and RT-Thread adapter. Its successful log already shows RX=7/TX=9 before MFi completes; the latest failure has no initial RX completion. Fourteen SDK C/header overlay files are unchanged and the published SDK gitlink remains `894575c161ffcfaa5862a06858bb254bf2c03776`.

| Later revision | Actual change | Relationship to this failure |
| --- | --- | --- |
| Parent `7784b996` | Retire old EHCI IAA before scanning new INT/ERR completions | Changes the controller completion lifecycle and belongs in the regression range; Host reproduction proves a lifetime defect in the older order, not that the new order works on this board. |
| Parent `6e950099`, driver `efc664b` | Switch lwIP input from direct to queued; remove normal RX acquisition of the TX mutex | First-RX submission timing and USB/lwIP scheduling change. Queued protocol dispatch happens after USB completion, so it does not directly explain zero completions; interactions still need board isolation. |
| Parent `fa9439c0`, driver `c117dcd` | Opt-in host-session activation before first RX, observed Carrier remains required | Added after the first no-RX failure and cannot have introduced that earlier failure. Latest log proves it did not restore the link. |

The separate Carrier worker, first-RX readiness guard, concurrent iAP2/network startup and supplementary invitation policy were already present in the accepted source snapshot. They are not new regressions in this range. No board test has isolated an individual bad commit; none is declared the root cause. The proper next experiment is a same-phone/cable/power-state confirmation with the retained accepted image, followed by isolated transport changes, rather than another speculative protocol capability or forced-link change. The accepted image has separately reported music-crash/stutter issues and is a startup comparison baseline, not a stable release recommendation.
