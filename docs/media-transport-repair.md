[English](media-transport-repair.md) | [简体中文](zh-CN/media-transport-repair.md)

# Media and wired transport repair

## Evidence and acceptance boundary

The user reported that the previous audio image still had remote-call echo and poor music. Its synthetic echo test and successful builds did not establish physical acceptance. The supplied music log contains repeated positive gaps of 16384 samples at 44100 Hz, approximately 372 ms. This is a gap between successfully decoded packets, not proof of Wi-Fi packet loss, a specific parser failure, or a 64 KiB TCP window. The previous image remains a failed functional baseline for these two user observations.

The historical wired measurements establish long USBMUX port-lock holds and Carrier/RX delays. They do not prove a sole cause for every video stall. NCM parsing constraints are retained; format-validation errors and network-delivery errors remain separate diagnoses.

## Implemented changes

### Audio receive and recovery

The portable receiver services audio UDP before video and drains at most 32 ready data datagrams per stream per owner iteration. Each additional receive requires a zero-timeout readiness check, so ports do not need nonblocking sockets and an empty socket cannot stall the owner. The bound preserves time for other streams and control traffic. Authentication and replay rejection remain mandatory.

Application media queues hold 32 compressed packets for lanes 2 and 7; voice lanes retain 16 and local audio retains 4. This bounded reserve handles receive bursts without an unbounded latency queue. A large positive RTP timestamp hole no longer discards already decoded PCM. Small holes retain the existing bounded silence policy. Queue overflow, lock contention and decoder failures must still be checked on the board; these changes do not establish where the previously missing packets were lost.

The application playback and microphone workers run at priority 13 with a 5-tick timeslice, ahead of video at 14 and receiver control at 15. Network service remains higher priority. Bounded decode-ahead, DAC backpressure and empty-capture waits preserve yielding; this scheduling policy needs board CPU and video regression measurements.

### Full-duplex echo processing

The application estimates additional acoustic/reference delay from strongly correlated reference and capture samples. It examines delays from 0 through 200 ms in 5 ms steps every tenth capture frame and requires three consistent observations before changing the delay. Silence and low-correlation near-end speech do not train the estimator. A manual delay disables automatic estimation; auto reenables it. A changed delay recreates the voice processor in its owning microphone worker.

The portable SpeexDSP voice processor uses a 200 ms echo filter and explicit residual-echo suppression, with weaker suppression during near-end activity. A missing reference feeds a zero reference through the echo processor instead of bypassing its timeline. This does not reconstruct missing playback samples. Reference alignment, clipping, processing headroom and actual double-talk intelligibility still require acoustic acceptance.

```text
carplay_status
carplay_audio_path
carplay_mic
carplay_mic delay auto
carplay_mic delay 20
carplay_mic aec off
carplay_mic aec on
```

### USBMUX and IPHETH

USBMUX IN and OUT use independent persistent endpoint mutexes, while EP0 retains the port mutex. The endpoint lock covers toggle read, submission, completion/cancellation and toggle update. Disconnect withdraws the published device and drains both endpoint owners before CherryUSB can release the port; queued callers recheck publication under the endpoint lock. Generation checks and failed-cancellation DMA quarantine remain.

IPHETH Carrier queries run in a separate control worker. Its private control storage is serialized against endpoint-halt recovery and initial configuration. The RX worker continues to consume and rearm its one URB while Carrier waits. Three consecutive failed Carrier transactions are required to declare a previously linked interface down; a successful explicit down result still takes effect immediately. Both workers acknowledge shutdown before their synchronization resources are released. Failed creation of either worker rolls back the network adapter and all allocated resources.

### Cached EHCI descriptors

The maintained generated HCD is produced by the reviewed patch, not by modifying the upstream source. A cached predecessor QH shares its horizontal link and hardware-updated overlay in one cache line. Before unlink modifies that line, the HCD disables only the affected schedule and waits for its hardware status to become inactive, then invalidates both descriptors, changes the link, cleans the predecessor and restores the prior enable bit. The other schedule is preserved. A bounded quiescence failure leaves descriptors and DMA ownership intact and reports failure instead of unlinking/freeing active memory. IAA still protects asynchronous reclamation.

This intentionally favors correctness over an unproven lock-free cached-descriptor update. It pauses the affected controller schedule briefly at unlink. Its physical timing and throughput must be measured; the implementation does not claim zero scheduling overhead. Merely invalidating before a clean without quiescing DMA would leave the original race.

## Automated verification

```sh
sh packages/third-party/cherryusb-luban/tests/run.sh
sh packages/third-party/cherryusb-luban/tests/run_control_probe.sh
cmake -S packages/third-party/cherryusb-ipheth -B build-ipheth-repair -DIPHETH_SANITIZE=ON -DCHERRYUSB_ROOT="$PWD/packages/third-party/cherryusb"
cmake --build build-ipheth-repair -j8
ctest --test-dir build-ipheth-repair --output-on-failure
make -C packages/third-party/rt-carplay -j8 check
make -C application/rt-thread/rtcarplay BUILD=build-network-validation -j8 check
```

The added regressions cover a 40-packet encrypted UDP burst across the 32-packet fairness boundary, RX progress during a deliberately slow Carrier transaction, both worker-creation failures and all five OSAL-allocation failures, preservation of a predecessor's independently advanced DMA overlay, schedule-quiescence timeout retention, preservation of the other schedule, and synthetic delay estimation with unrelated near-end speech and silence rejection. Host results and image packaging results are reported separately in the delivered validation directory.

## Required board acceptance

Record the image SHA256 and retain complete serial output. During music, take carplay_status and carplay_audio_path snapshots before playback and after at least 20 seconds of the same downloaded music. Compare decoded count, decode_err, decode_max_us, arrival_max_ms, overflow, queue_busy and output device errors as deltas. Repeat wireless and wired, then call/Siri interruption and return to music. Audio transport authentication and queue failures must not be dismissed as a codec-quality problem.

During a call, use a headset at the remote end and a fixed moderate vehicle speaker level. Test far-end-only speech, near-end-only speech and double talk; compare AEC enabled and disabled and preserve carplay_mic counters. Confirm auto_delay, delay_updates, reference hits/misses, capture gaps, clipping and processing time relative to a 10 ms frame. Persistent reference misses or excessive CPU time are a failure, not successful AEC merely because its flag is enabled.

Exercise sustained wired video/touch/audio, physical unplug/replug and automatic reconnect. Repeat the existing Carrier on/off/on experiment; slow Carrier should no longer delay RX rearm. Check long-tail rearm/parse/TX times and classified RX errors. Run Ethernet sharing concurrently and retain the prior user-verified Internet behavior as a regression requirement. Until these measurements are obtained for the replacement image, music quality, echo reduction and physical wired-stall improvement remain pending.

## Follow-up: UDP loss before the SDK and wired link notification

The replacement-image board trace still showed 44.1 kHz AAC frames followed by positive timestamp gaps of 16384 samples. That is 16 missing 1024-sample frames (about 372 ms). A decoded-timestamp gap alone does not locate the loss. Inspection found that the RTL target inherited DEFAULT_UDP_RECVMBOX_SIZE=1 and RECV_BUFSIZE_DEFAULT=8192. The higher-priority lwIP thread can deliver a burst before the CarPlay owner runs; recv_udp then frees packets when either limit is exceeded. Increasing the application's compressed queue does not recover those packets.

The new host regression executes the actual lwIP 2.1.2 recv_udp callback, with only the OS mailbox modeled. A 17-packet burst of 736-byte datagrams (700-byte AAC plus the 36-byte transport envelope) reproduces exactly 16 discarded packets under the old limits. Enlarging only the slot count retains 11 packets before reaching 8192 bytes. With the new 64-slot/65536-byte limits it retains all 17, and all 64 in a larger burst; a 65th packet is still rejected by the slot bound. A PCM-size burst separately exercises the byte bound. This proves a software defect sufficient to cause the symptom, not that this exact synthetic burst was captured from the phone.

Both maintained CarPlay defconfigs now set RT_LWIP_UDP_RECVMBOX_SIZE=64 and RT_LWIP_RECV_BUFSIZE=65536. Other targets retain the previous defaults, including AIC8800's 32 slots. The regression compiles the actual port lwipopts.h with both defconfigs and both legacy defaults. The byte ceiling is a limit, not a preallocated payload arena; the global pbuf limit and finite mailbox remain effective. The receiver now polls mDNS without waiting and waits on session sockets for up to 10 ms, avoiding an unrelated 10 ms mDNS delay on every audio dispatch.

carplay_status separates delivered payloads, sink_rejected, malformed, replay drops, sequence_missing and sequence_reordered. Sequence counters are observed after successful payload authentication, but the RTP sequence field itself is outside the authenticated AAD; these are diagnostic observations, not authenticated loss proof. A forward gap can subsequently be filled by reordering/retransmission, so sequence_missing is not a final loss total. poll_gap_max_us measures intervals between readable audio/control dispatches and includes silence; it is not a CPU-blocking measurement. AAC rejection logs report at most the first three decoder failures, including error, consumed bytes, sample count, channels and rate. Codec validation, authentication and replay protection remain enforced.

The wired trace advanced from stage=3 directly to stage=6, with zero received video frames. It exited while waiting for a valid wired IPv6 endpoint, before stage=4 starts iAP2, so it does not establish a video decoder failure. A separate reproducible defect exists when the shared Ethernet RX mailbox is full: the old IPHETH adapter drops its one link-up notification and does not retry while carrier remains up. This path is active when the physical Ethernet RX thread is enabled. The IPHETH adapter now uses the synchronous core-serialized netif API for link up/down and detach, matching RT-Thread's no-RX-thread behavior and bypassing that shared mailbox. The actual adapter regression confirms that link up/down succeeds with a modeled full Ethernet mailbox. It does not force carrier up or bypass IPv6 DAD. The connection owner records the stage, return code, attachment and mode-change state before cleanup; identifying this particular board failure still requires complete logs and ipheth_status/ifconfig at the failure.

Run the added regression with make -C application/rt-thread/rtcarplay check-udp-burst. It is also included in the normal application check target. Record carplay_status and carplay_audio_path before and after at least 20 seconds of wireless downloaded music, then test Siri/call interruption and wired projection. For a wired startup failure retain the new wired-exit line, ipheth_status, ifconfig and carplay_connection status. Hardware music quality, echo performance, wired first frame/reconnection and Ethernet sharing regression remain NOT_RUN for this replacement image until measured on the board/iPhone.

## Subsequent startup acceptance

The later startup implementation starts iAP2 at stage 3 while networking progresses, rather than waiting until stage 4. The user confirms wired CarPlay works after the invitation-recovery image, and its supplied log reaches visible video and stage 5 after one supplementary invitation. See [wired startup](wired-startup.md) for the current sequence and exact evidence boundary. Audio PCM reception is present, but audio timestamp gaps and an Internet-sharing timer-pool assertion remain visible. Physical echo, audio quality, repeated reconnects, touch and sustained performance have not received separate acceptance in that report.

The subsequent board test reported a wired music-playback crash (RISC-V load access fault: mcause=5, mepc=0x40069a22, mtval=0x0000006f216000d8, ra=0x4006b722) and continuing wired UI stutter. Startup and picture acceptance therefore does not establish playback or stability acceptance. The delivered GUI symbol table places the PC in ehci_urb_waitup and the return address in USBH_IRQHandler, but the delivered IMG bytes at the PC encode a branch, not a load. Confirm the flashed image identity before attributing the fault to an exact instruction or claiming a fix. Audio stack telemetry reports 98304 total bytes and 1876 peak used bytes; this sample does not establish audio stack exhaustion. USB URB/QH lifetime, cache coherency and memory corruption remain investigation candidates, not confirmed root causes. No crash or stutter fix is claimed by this publication.
