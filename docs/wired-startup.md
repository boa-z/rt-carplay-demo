[English](wired-startup.md) | [简体中文](zh-CN/wired-startup.md)

# Wired startup and pairing performance

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

The subsequent board test reported a wired music-playback crash (RISC-V load access fault: mcause=5, mepc=0x40069a22, mtval=0x0000006f216000d8, ra=0x4006b722) and continuing wired UI stutter. Startup and picture acceptance therefore does not establish playback or stability acceptance. The delivered GUI symbol table places the PC in ehci_urb_waitup and the return address in USBH_IRQHandler, but the delivered IMG bytes at the PC encode a branch, not a load. Confirm the flashed image identity before attributing the fault to an exact instruction or claiming a fix. Audio stack telemetry reports 98304 total bytes and 1876 peak used bytes; this sample does not establish audio stack exhaustion. USB URB/QH lifetime, cache coherency and memory corruption remain investigation candidates, not confirmed root causes. No crash or stutter fix is claimed by this publication.
