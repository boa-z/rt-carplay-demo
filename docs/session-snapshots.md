[English](session-snapshots.md) | [简体中文](zh-CN/session-snapshots.md)

# Session snapshots and endpoint ownership

## Copied metadata

`carplay_app_iap2_info()` and `carplay_app_wired_info()` copy session strings into arrays in the caller's structure. The copied name, SSID, passphrase, device identifier and public key remain valid after Receiver stop/restart. `source_version` alone points to an immutable SDK constant. Callers must rebuild against the updated host header and fill array members with copies instead of assigning string pointers. This changes the Luban-Lite application API, not the portable Receiver API.

`carplay_app_display_info()` copies geometry and the pairing PIN. The launcher and startup command use this copy. The compatibility geometry getter returns zero during a lifecycle change. The legacy pointer-returning PIN getter remains restricted to the serialized lifecycle owner; cross-thread users must use the copied display structure.

## Query and lifecycle ordering

Readers enter a shared atomic count outside the resettable session. They can coexist with other readers and touch/Siri requests. Start/stop serializes lifecycle entry, prevents new readers and sleeps until existing readers return before resetting state or destroying the session mutex. Queries arriving during this retirement return `RTCP_ESTATE`. The display output is cleared on failure. All operations are ordinary-thread APIs, not interrupt handlers.

Debug counter queries hold a reader reference while accessing the session mutex; HID counters and encrypted-event readiness use atomics. The shell status command shares the existing lifecycle gate and copies queue counters under the queue mutex. Pairing persistence publishes its actual write result in a separate atomic that survives session reset. Query contention is never reported as a pairing-storage failure.

## CarPlay Control commands

The poll worker owns the mDNS Browser, endpoint selection and Control connection counters. `carplay_ctrl on|off|connect` only queues requests under the session mutex and lifecycle gate. The worker applies them before its next Control tick. `connect` preserves the existing retry interval calculation and does not implicitly enable a disabled browser. Multiple pending enable requests coalesce to the latest desired value; repeated connect requests coalesce to one retry request.

`carplay_ctrl status` prints a copied owner-published snapshot, including the endpoint strings, and reports pending requests separately from the applied state. It never calls Browser APIs from the shell. Publication may lag one poll tick when the mutex is busy; network I/O does not hold that mutex. An inactive/retiring receiver or a Control command without an available browser returns `RTCP_ESTATE`; mutex contention returns `RTCP_ETIMEDOUT`, and invalid arguments return `RTCP_EINVAL`. A running wired receiver can report Control as unavailable through `status`.

## iAP2 callback buffers

Wireless and USB iAP2 links keep their own endpoint structure. Wi-Fi and AirPlay provider callbacks return pointers into that persistent link storage, not a temporary stack snapshot. A successful query updates the link copy. A failed query leaves the last valid copy intact, including pointers retained by the session identity configuration. The link owner must still retire its iAP2 session before resetting that link storage.

This changes ownership only. Capability advertisement, Pair-Setup/Pair-Verify, iAP2 message timing, endpoint values, audio negotiation and media behavior remain unchanged.

## Worker startup and quiescence

Bluetooth Echo publishes its atomic running state before scheduling, serializes commands and checks startup errors. Create/start failures remove its callback and release its Bluetooth claim. Stop before first scheduling skips controller enable and listener registration. Stop cannot join its own worker; a bounded timeout leaves ownership with the worker until it completes disconnect/callback cleanup and publishes quiescence as its last operation. Stopping an inactive Echo does not disconnect another Bluetooth owner.

The wired USB iAP2 worker similarly checks thread startup and rolls back its session and successfully installed MFi provider on failure. Failed provider installation never releases another owner's reference. Its run/running flags and thread handle are atomic; reset leaves atomic storage intact for concurrent observers. A detach generation prevents startup from overwriting an earlier detach request. A stop queued before scheduling skips link-up transmission, and the final stopped log and provider cleanup precede the quiescence flag. Stop rejects self-join and retains the stream on a three-second timeout; shell `stop` also retires the stream owner before closing the USB hook.

USB application operations and every shell command share one nonblocking atomic call gate. Contention returns `RTCP_ESTATE` without entering Lockdown or transport code. This serializes individual operations, including status commands with reusable descriptor storage, but callers still need one application owner for a multi-operation pair/start sequence. The detach callback remains nonblocking and only publishes the stop flag/generation. The trust-cache result is atomic. Public session close reports `RTCP_ETIMEDOUT` for a timeout or rejected self-join and preserves the stream; a later close can finish after the worker retires.

The wireless iAP2 service serializes start/stop with an atomic lifecycle gate; contention and self-join return `RTCP_ESTATE`. Its run flag, worker handle, running state and cross-owner progress/counters are atomic. Bootstrap and AirPlay tunnel resets store each diagnostic field individually rather than clearing live atomic storage with `memset`. Status reads never dereference a session; they copy observational fields that may reflect adjacent owner steps, not one transactional session transition.

An asynchronous wireless stop only publishes a request generation and stop flag. The generation prevents a request arriving during Bluetooth claim or state reset from being overwritten by startup. The RFCOMM owner observes stop after its existing bounded 500 ms accept/100 ms receive wait or an in-progress platform startup call, and performs transport disconnect itself. Stop before first scheduling skips MFi probing and Bluetooth enable entirely. Stop during MFi probing, Bluetooth startup, session creation or successful accept cannot proceed into a later protocol link-up. A synchronous stop waits up to three seconds; timeout preserves worker ownership and rejects restart until final cleanup. Only a successfully installed MFi provider is released, so a failed installation cannot decrement another service's provider reference. Session destruction, provider release, Bluetooth claim release and the final log all precede publication of quiescence.

## Touch reader retirement

Touch start/stop share a nonblocking call gate; contention or self-join returns `RTCP_ESTATE`. Run flags, point count and diagnostic counters are atomic and reset with stores, not a concurrent `memset`. Stop waits up to one second for the reader; `RTCP_ETIMEDOUT` retains the device, point buffer, wake semaphore and borrowed callback/context. Restart remains rejected even if the reader exits later, until a repeated stop closes those resources. The receiver shutdown owner retries stop before retiring callback state. A blocked device read can therefore delay shutdown; a timeout is not permission to free that context.

The actual-source touch regression checks existing ownership, IRQ and coordinate behavior plus gate contention, self-join, timeout retention, late reader retirement followed by cleanup retry, and 32 immediate-stop restart cycles under ASan/UBSan. The device/I2C scheduler remains modeled; physical IRQ delivery and touch response require board testing.

## Audio output retirement

Audio output start/stop share a nonblocking lifecycle gate. Contention and writer self-join return `RTCP_ESTATE`. Public enqueue, query and diagnostic/volume/probe/path commands retain a reader reference through their final mutex/semaphore access. Lifecycle retirement rejects new readers and waits for accepted readers before resetting sink storage or deleting resources. Ordinary readers coexist and the existing two-millisecond packet queue-lock wait is unchanged. During retirement, packet/local enqueue returns `RTCP_ESTATE`, queries return false/zero/-1 and void commands are ignored. Diagnostics remain observational multi-field values.

Stop then waits up to five seconds for the writer. `RTCP_ETIMEDOUT` retains the device, mutex, semaphore, mixer and jitter queues; restart remains rejected even after late writer exit until a repeated stop releases them. The receiver shutdown owner retries stop before completing teardown. A blocked output device or accepted reader can therefore delay teardown; timeout does not authorize freeing its context. Lifecycle calls are ordinary-thread APIs and must not run from device callbacks or sink workers.

The actual audio regression retains its PCM/AAC sample, volume, Direct/Mixer, critical-alert and probe checks. Additional real-pthread cases verify writer self-join rejection, a blocked device write causing timeout, retained handles, late cleanup retry, a reader blocked inside the queue mutex while stop waits, refusal of all public/shell readers during retirement, and 32 immediate-stop restarts with concurrent status readers. The original late-retirement scenario fails against the historical source and passes against the fix. ASan/UBSan is not ThreadSanitizer or audible hardware proof. Device helpers, decoding and the complete playback worker remain byte-identical to the preceding baseline; no audio negotiation, codec, jitter, mixing, gain or AEC policy is changed.

## Video retirement and shell ownership

Video lifecycle operations and display-observer registration share a nonblocking gate. They retire accepted frame/queue/query readers before resetting storage or freeing a mutex, semaphore or decoder. Contention and worker self-join return `RTCP_ESTATE`. Stop waits up to three seconds for the decoder; timeout retains decoder/frame/display/observer ownership and blocks restart until a repeated stop completes cleanup. The receiver shutdown owner retries stop. The stopped observer is notified only after decoder retirement and resource cleanup, under lifecycle exclusion; callback reentry is rejected.

Run flags and diagnostic counters are atomic. Statistics remain observational across fields; the existing single-core interrupt-masked performance copy is retained. During retirement, queries return false/zero and incoming frame callbacks are ignored. Shell calls serialize their own report window and file feeder; `status` retains a reader until its final access. In a Launcher build, manual `start`, `stop` and `play` return `RTCP_ESTATE` so they cannot replace its video ownership; read-only status remains available. Manual bring-up remains available with the Launcher disabled.

The actual-source regression runs with both manual and Launcher configurations, retaining atomic SPS/PPS, IDR recovery, queue backpressure, scanout lifetime and first-frame/HMI tests. It additionally checks stop timeout/late cleanup, self-join, modeled reader retirement, observer reentry rejection, command exclusion and 32 immediate-stop restarts. Device helpers, decode/recovery and the complete decoder worker remain byte-identical to the preceding baseline. This is modeled MPP/DE evidence, not board frame-rate or visible-output acceptance.

## Executed regressions and remaining audit

`make -C application/rt-thread/rtcarplay -j8 check` runs the actual composition against real Linux protocol/entropy ports and modeled RT-Thread scheduling/device APIs. It checks copied wired/wireless/display metadata, IPv6 readiness, retained snapshots after source clearing, concurrent readers, writer waiting for a real pthread reader, rejection during teardown and the existing 32 lifecycle cycles. The actual iAP2 provider regression verifies returned pointer ownership, source retirement and failed refresh without invalidating retained strings. The composition regression also parses an actual DNS SRV response with the real Browser, checks queued versus applied Control commands, copies endpoint data across Browser destruction, and verifies command rejection during teardown. Both run under ASan/UBSan in the host CI job.

`check-wireless-iap2-worker` compiles the actual wireless state, callbacks, owner, tunnel and start/stop functions with real SDK iAP2 sessions and modeled Bluetooth/scheduling/MFi-probe boundaries. It covers create/start failures, lifecycle contention, stop during claim, before scheduling and during probe/enable/session creation/accept, failed-provider-install ownership, self-join, bounded timeout retention, and 32 restart/tunnel-reset cycles with a concurrently running status reader. ASan/UBSan checks memory/undefined behavior; this is not a ThreadSanitizer result or a wire-level MFi handshake test.

`check-usb-iap2-worker` extracts the actual link type, reset/cleanup, worker, call gate and public/shell wrappers, links real SDK iAP2 sessions and models thread scheduling, provider release and blocking command I/O. It covers create/start rollback, failed-install cleanup ownership, detach during bootstrap/thread creation, stop before scheduling, self-join, timeout retaining the stream, a real competing pthread rejected by all wrappers, error-path gate release and 32 restart cycles under ASan/UBSan. It does not exercise real Lockdown trust or the physical USB scheduler.

These tests do not establish physical pairing, reconnect or playback. Other device worker lifetimes still need audit. Complete V2.1 acceptance and subsequent Launcher V2 work remain open.
