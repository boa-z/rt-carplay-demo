[English](vehicle-protocol-demo.md) | [简体中文](vehicle-protocol-demo.zh-CN.md)

# D213 Vehicle State protocol bench

RTCARPLAY_VEHICLE_PROTOCOL_DEMO is an explicit, default-off development option. The unified D213 defconfig leaves it disabled, so ordinary images contain no simulated vehicle; enable it in menuconfig for protocol experiments. It attaches a simulated combustion-engine model to the existing authenticated Receiver Vehicle State lanes and opts into vehicleStateProtocol negotiation. This makes the application available for future real-iPhone protocol experiments. It does not establish official Ultra, actual vehicle data or official Local UI rendering.

Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI). The metadata is re-expressed from native/crates/livi-cp/src/vehicle_state/engines.rs and the RPM unit constant in vehicle_state.rs at revision 64a05b68358db52ed887169143afd5944fd6f1f7, under GPL-3.0-or-later and its NOTICE attribution requirement. See the bundled rt-carplay NOTICE and the application's visible legal notices. No theme assets, keys or private device material are included.

## Implemented model

The plugin name is rt-carplay SIMULATED and its locally assigned plugin ID is 0. The model contains one combustion-engine accessory (IID 2560, type 0x09000005), one service (IID 2561, type 0x1A00000F) and exactly these characteristics. IIDs identify this local catalogue, not a universal allocation for every vehicle.

| IID | Characteristic type | Meaning | Initial value | Behavior |
| --- | --- | --- | --- | --- |
| 2562 | 0x30000024 | Engine rotational speed | 0 RPM | Car-owned, mutable by the local simulator, phone writes refused. |
| 2563 | 0x3000004E | Maximum engine rotational speed | 6000 RPM | Fixed; local and phone writes refused. |
| 2564 | 0x3000004F | Engine redline marker | 5000 RPM | Fixed; local and phone writes refused. |

All three are Uint32, use unit 981 (revolutions per minute), and declare range 0–6000 with step 1. The simulator samples every 250 ms, rising from 0 to 5000 RPM and falling to 0 over ten seconds. This cadence is an application data-source choice, not an inferred protocol deadline, presentation clock or vehicle sampling requirement. The simulator never commands a vehicle.

Road speed, gear and battery SOC are deliberately absent from this wire catalogue. They remain separate explicitly simulated local dashboard values. The reference set_gauges function maps rpm to ENGINE_RPM and fuelPct to FUEL_LEVEL; it does not map speedKph. The speedKph key in vehicle_state/tests.rs appears among additional inputs in gauges_reach_the_phone_in_both_units, while its asserted notification contains fuel/range/odometer/consumption data. That test is not evidence of a road-speed characteristic. Fuel percentage and electrical engine power are also not battery SOC. Real mappings and the complete display/plugin/vehicle relationships remain to be established.

## Lifecycle and diagnostics

The application creates the model before building /info, supplies that same model to the Receiver, and enables vehicleStateProtocol only if the authenticated phone offers it. Each new controller refreshes /info from current values on the Receiver owner before validation; the active session retains its immutable capability snapshot. Receiver polling owns simulation updates and protocol handling. Subscriptions reset at authentication loss, while the source continues; application stop destroys the Receiver before releasing its borrowed model. A full application restart starts a new simulator model.

The SDK handles config/read/register, changed subscribed HIGH notifications, and explicit per-IID errors for refused writes. Its existing transport backpressure and session reset rules apply. Only the three implemented values are declared. UI Sync, displayPanels, HEVC, altScreen and Type 111 remain absent. There is no renderer or asset package behind this experiment, and a phone can decline to negotiate or display the model.

Use carplay_status to inspect the owner-published snapshot: SIMULATED engine RPM, source error, phone offer, accepted enablement, normal/HIGH lane states, requests/rejections, subscriptions/notifications, transport queue/retry/abandonment counters and last delivery error. Lane states are 0 idle, 1 listening, 2 connected, 3 closed, 4 failed. The snapshot is refreshed at the existing approximately 100 ms diagnostics interval; stopped applications can retain the last published sample, so read the leading running/stopped line too. A queued notification is not phone receipt, display or physical actuation. No shell thread dereferences the live model or Receiver.

## Validation and pending board work

Run make -C application/rt-thread/rtcarplay check-vehicle-protocol-demo on Linux/WSL. The actual application composition is compiled both with and without the option. The enabled variant checks attachment, initially unoffered/disabled lanes, absence of UI registration, refreshed /info after a changed value, startup failure rollback and repeated start/stop destruction. The model test covers fixed values, subscription responses, HIGH notifications, same-slot suppression, refused phone writes, triangle endpoints, reset subscription clearing and recreation. Python plistlib independently decodes the generated /info and verifies every characteristic's identity, type, bounds, unit, ownership and initial value, plus the absence of UI/panel/HEVC declarations.

These are Host checks. Before flashing, export and verify a fresh backup of the current identity/pairing files; restore and read back their hashes afterward. Then verify the SIMULATED diagnostic line on COM9, preserve sanitized phone feature/SETUP and lane evidence, and test real config/read/register/HIGH notification behavior while regressing Classic video/audio and reconnect. A lack of phone offers is an observation, not a successful Ultra negotiation. Physical flashing, real-iPhone negotiation/interactions and presentation remain NOT_RUN for this milestone; L0 PASS, full-scope L1 PARTIAL and L2–L5 NOT_RUN are unchanged.

## Current acceptance and manual interoperability capture

The current goal excludes complete official Ultra. The milestone results above are historical; the [current acceptance table](ultra-development.md#current-scope-and-acceptance) takes precedence. Historical real-phone evidence confirms Vehicle State offer/enable, both connected lanes and one processed request with a queued response, but did not identify its method. The user has subsequently confirmed Fusion visuals and latest fullscreen, settings persistence and automatic handoff. These UI confirmations do not establish Vehicle State read/write/subscription acceptance.

The existing firmware already prints `vehicle method unknown/config/read/register/write/control` request/response totals and `vehicle last` method/lane/result/valid fields. Use the current image's diagnostics rather than interpreting the historical single-request count as a particular command. No new test code or automatic board access is needed for this capture.

1. Record the installed artifact name, source revision and image SHA256 from its manifest when available. Record phone model/iOS and whether the session is wired or wireless; remove identifying addresses, names and pairing material before sharing logs.
2. Before connecting the phone, run `carplay_status` and save a baseline. Check the leading running/stopped line. The snapshot is owner-published, so allow it to refresh before drawing conclusions.
3. Connect normally using the user-controlled board. Run `carplay_status` again once connected. Save `offered`, `enabled`, Normal/HIGH states, all per-method totals, last-method fields, subscription/notification counts and delivery errors/counters. Repeat after the simulator has run for a few seconds. Only subtract counters within the same model lifetime; application restart resets them, while authentication/session reset preserves cumulative diagnostics.
4. If the phone sends register and subscription count becomes nonzero, compare later notification and transport counters while RPM changes. A locally serialized response or notification and an increase in `queued` are separate observations; neither proves phone receipt or presentation. If the phone does not request a method, mark that interaction NOT_RUN rather than synthesizing a successful phone result.
5. During a user-controlled disconnect/reconnect, capture another baseline and status. Check subscription reset and new negotiation without assuming counters reset. Check Classic video/audio and local instruments separately. Repeat on the other transport when available; one transport's result does not accept the other.

| Observation | What it establishes |
| --- | --- |
| `offered=1 enabled=1` and connected lanes | The bounded Vehicle State negotiation and transport observation; no UI Sync or official-product conclusion. |
| A method's request and response totals increase | Local parsing and serialization for that method. Inspect refusal/commit semantics before accepting a write. |
| Register request, nonzero subscriptions, changing notification/queue counters | Local subscription and notification progression; phone receipt/display still needs independent evidence. |
| Write/control attempts | This particular three-value model rejects phone writes and declares no controls. Successful writable/control behavior requires a separately established catalogue and is not an expected result here. |
| UI Sync or Type 111 absent | Expected in these D213 profiles; their capabilities remain disabled. |

Preserve a sanitized status capture with the artifact identity and mark each observed interaction PASS/FAIL, unattempted or unavailable interaction NOT_RUN, and a demonstrated protocol/access impediment BLOCKED. Do not publish raw pairing files or identity logs. Real vehicle-speed/gear/SOC mappings remain unconfirmed and must not be inferred from the local simulated dashboard.
