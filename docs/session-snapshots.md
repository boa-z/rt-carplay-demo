[English](session-snapshots.md) | [简体中文](zh-CN/session-snapshots.md)

# Session snapshots and endpoint ownership

## Copied metadata

`carplay_app_iap2_info()` and `carplay_app_wired_info()` copy session strings into arrays in the caller's structure. The copied name, SSID, passphrase, device identifier and public key remain valid after Receiver stop/restart. `source_version` alone points to an immutable SDK constant. Callers must rebuild against the updated host header and fill array members with copies instead of assigning string pointers. This changes the Luban-Lite application API, not the portable Receiver API.

`carplay_app_display_info()` copies geometry and the pairing PIN. The launcher and startup command use this copy. The compatibility geometry getter returns zero during a lifecycle change. The legacy pointer-returning PIN getter remains restricted to the serialized lifecycle owner; cross-thread users must use the copied display structure.

## Query and lifecycle ordering

Readers enter a shared atomic count outside the resettable session. They can coexist with other readers and touch/Siri requests. Start/stop serializes lifecycle entry, prevents new readers and sleeps until existing readers return before resetting state or destroying the session mutex. Queries arriving during this retirement return `RTCP_ESTATE`. The display output is cleared on failure. All operations are ordinary-thread APIs, not interrupt handlers.

Debug counter queries hold a reader reference while accessing the session mutex; HID counters and encrypted-event readiness use atomics. The shell status command shares the existing lifecycle gate and copies queue counters under the queue mutex. Pairing persistence publishes its actual write result in a separate atomic that survives session reset. Query contention is never reported as a pairing-storage failure.

## iAP2 callback buffers

Wireless and USB iAP2 links keep their own endpoint structure. Wi-Fi and AirPlay provider callbacks return pointers into that persistent link storage, not a temporary stack snapshot. A successful query updates the link copy. A failed query leaves the last valid copy intact, including pointers retained by the session identity configuration. The link owner must still retire its iAP2 session before resetting that link storage.

This changes ownership only. Capability advertisement, Pair-Setup/Pair-Verify, iAP2 message timing, endpoint values, audio negotiation and media behavior remain unchanged.

## Executed regressions and remaining audit

`make -C application/rt-thread/rtcarplay -j8 check` runs the actual composition against real Linux protocol/entropy ports and modeled RT-Thread scheduling/device APIs. It checks copied wired/wireless/display metadata, IPv6 readiness, retained snapshots after source clearing, concurrent readers, writer waiting for a real pthread reader, rejection during teardown and the existing 32 lifecycle cycles. The actual iAP2 provider regression verifies returned pointer ownership, source retirement and failed refresh without invalidating retained strings. Both run under ASan/UBSan in the host CI job.

These tests do not establish physical pairing, reconnect or playback. The remaining audit includes the legacy CarPlay Control shell/browser ownership and Bluetooth/iAP2/device worker lifetimes. Complete V2.1 acceptance and subsequent Launcher V2 work remain open.
