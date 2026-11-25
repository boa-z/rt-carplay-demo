[English](connection-manager.md) | [简体中文](zh-CN/connection-manager.md)

The manager also owns the temporary maintenance lease used by [Wi-Fi STA](network-manager.md) and [firmware installation](ota.md). It closes USB and wireless session owners and acknowledges quiescence before another service takes the resource. Releasing the lease resumes the selected connection policy.

# Automatic application connection manager

## Responsibility

`app/connection_manager.c` owns the application connection worker, Receiver start/stop, USB pairing, wireless iAP2 handoff, stage publication, pairing confirmation, device trust records and audio-test scheduling. It has no LVGL dependency. `ui/lvgl_launcher.c` owns presentation and the pointer queue, posts requests and reads caller-owned snapshots. Protocol endpoint teardown stays in the independent SDK Receiver.

## Startup and lifetime

`RTCARPLAY_AUTO_CONNECT` enables the same application owner with or without LVGL. Launcher V2 selects it and starts it after UI/input registration. Headless starts it from `main()`. With both wireless options present, boot arms USB discovery and starts SoftAP/mDNS/Bluetooth/iAP2 without requiring a mode selection. The USB configuration hook is enabled in static initialization before CherryUSB starts, so an already plugged-in phone can enumerate before GUI startup; the later owner reads its retained snapshot. Manual builds retain explicit USB enable behavior.

`carplay_connection_manager_start()` is called by one startup owner, is idempotent after successful startup, and returns `RTCP_ENOMEM` or `RTCP_EIO` on allocation/scheduling failure. The GUI startup owner retries failed worker startup once per second; Headless main retries at the same interval. Successful startup is idempotent and creates no second worker. The worker and mutex have application lifetime; no manager destruction API is exposed. A failed USB service open retries every five seconds while wireless remains available.

## Arbitration and recovery

Only one Receiver and one application session owner run. During idle wireless discovery, an attached USB phone wins if no Bluetooth, handoff or AirPlay contender has been observed. Once wireless pairing/handoff starts, USB waits until that attempt ends; it never steals an active session. A USB attempt retires the wireless iAP2 owner, provider and Receiver before starting its IPv6 Receiver. USB enumeration stays armed during wireless operation. The unselected radio session is paused during a wired attempt/session because the board shares Receiver/media/MFi resources; simultaneous standby does not mean two concurrent protocol sessions.

Wireless retains the existing bond restore, RFCOMM service, MFi and Wi-Fi handoff sequence. Its existing owner pages saved bonds after two seconds and retries every 30 seconds, pausing paging during handoff or a live session. The manager keeps separate failed-attempt cooldowns of 5, 10, 20, 40 and then 60 seconds across automatic transport switches. When USB owns the connection, an attached iPhone keeps that ownership through wired retry. Removing it resets the USB cooldown and allows wireless discovery when enabled. Normal disconnect uses a one-second quiet period, and manual retry clears both cooldowns. Deadline comparisons handle the 32-bit tick wrap.

USB trust, IPv6 readiness and first-frame waits retain their existing transport limits. GUI LIVE requires `carplay_video_sink_has_presented()`: successful layer submission and vsync, independently of decoded-frame counters. Failed presentation can increase decoded counts but cannot mark the session LIVE. Headless uses the copied protocol session state because no physical display exists. Protocol session loss after LIVE triggers cleanup and reconnect even if USB/iAP2 remains attached. State snapshots may lag by one 100 ms publication tick; loss detection first observes an established session before interpreting a later false value as disconnection.

Cleanup waits for USB and wireless owners before stopping the Receiver. Receiver shutdown retries media retirement before releasing borrowed resources. A worker that cannot retire keeps ownership and blocks replacement; it is never force-freed for a faster reconnect. Host scheduling models verify this order; physical USB cancellation and radio/device progress still require board tests.

## Requests, settings and snapshots

`carplay_connection_status()` copies stage, errors, discovery status, pairing challenge, endpoint, counters and separate public Receiver readiness fields under a nonblocking manager lock. A false result means no available snapshot. Copies do not change after publication. Stage is a UI summary; transport, authentication, session, video, audio and input readiness stay separate.

Idle-only `carplay_connection_set_wireless_enabled()` selects automatic both-path discovery or USB-only operation. USB remains enabled. This setting is held in RAM and resets to both-path discovery at reboot. The legacy `carplay_connection_select_mode()` remains a manual diagnostic override, but Launcher V2 does not call it. `carplay_connection_retry()` accepts idle/failed requests. Bluetooth replies retain the current request token; the platform rejects stale confirmations.

Device and audio pages reserve connection ownership. Wireless standby retires iAP2/Receiver before record access; deletion also stops a wired Receiver before reloading persistence. File snapshots copy entries into UI-owned storage with revisions, so row callbacks never borrow a worker array. Device identity files are excluded from deletion. Bluetooth reset affects all saved bonds; the UI explicitly confirms that scope. Audio tests release the Receiver before opening sound0/dmic0 and do not resume it until stop succeeds. Gain/test commands are applied by the worker. Negotiation, decoding, jitter, Mixer/Direct, encrypted microphone uplink and AEC policy remain unchanged.

Touch is gated by the manager's closing stage and then queued through the Receiver owner's input interface. The launcher pointer mutex remains independent. Read-only settings and diagnostics never manipulate SDK objects. Manual lifecycle shell commands are blocked in automatic builds; use the manager command instead:

```sh
carplay_connection status
carplay_connection retry
carplay_connection wireless off
carplay_connection wireless on
```

## Validation

Run from the Luban-Lite repository:

```sh
make -C application/rt-thread/rtcarplay -j8 check
make -C application/rt-thread/rtcarplay check-auto-connections
make -C application/rt-thread/rtcarplay check-lubanlite-demo
make -C application/rt-thread/rtcarplay check-lubanlite-usb
make -C application/rt-thread/rtcarplay check-lubanlite-video
```

The actual automatic worker runs 32 repetitions of boot discovery, wireless reservation against USB arrival, USB cooldown/retry, asynchronous radio failure, USB-open failure, first-frame timeout, attached AirPlay loss, Receiver startup failure and clock wrap, under both GUI and Headless compilation. The actual USB Class binds before owner subscription in a separate automatic build. Video tests reject readiness after layer/vsync failure even with nonzero decoded counters. Existing manual-mode, trust/storage, manager exclusion, pointer overflow/routing, audio ownership, lifecycle and protocol tests are retained. Two syntax checks compile the manager without LVGL headers. [Launcher verification](mobility-cockpit-v4.md#evidence) adds actual LVGL rendering and UI event routing with modeled connection values.

Host ASan/UBSan, LVGL rendering and GUI/Headless cross-builds are separate evidence. This change's exact CI revision and images are recorded in its verification report. Physical D213 startup, display, USB/radio reconnection, persistence, audio and touch remain `NOT_RUN` until a matching image is tested.

## Receiver integration

The host composes public `rtcp_receiver_config_t.platform`, `.media` and `.iap2` descriptors and queues HID, Siri and tunneled iAP2 for the poll owner. `carplay_app_session_state()` copies owner-published public state without borrowing the protocol object. Video stays Annex-B H.264, and audio retains LPCM/AAC/Opus metadata and timestamps. The SDK remains independent, and cherryusb-ipheth remains an independent driver. See the SDK [Receiver API](https://github.com/boa-z/rt-carplay/blob/54df9892d618a4ac30af839c2a1636acf2217314/docs/receiver-api.md) and [lifecycle contract](https://github.com/boa-z/rt-carplay/blob/54df9892d618a4ac30af839c2a1636acf2217314/docs/lifecycle-and-ownership.md).
