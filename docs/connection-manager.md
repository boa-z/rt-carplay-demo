[English](connection-manager.md) | [简体中文](zh-CN/connection-manager.md)

# Application connection manager

## Responsibility

`app/connection_manager.c` owns the application connection worker, receiver start/stop, USB pairing, wireless iAP2 handover, connection stage publication, pairing confirmation requests, pairing-file operations and audio-test scheduling. It has no LVGL include or symbol dependency. `ui/lvgl_launcher.c` owns LVGL objects, display handoff policy and the launcher's pointer queue. The UI posts requests through `app/connection_manager.h` and reads copies of manager state. Protocol endpoint teardown remains the Receiver runtime's responsibility.

This extraction completes a boundary required by the V2.1 refactor. It preserves the existing USB/wireless mode selector and connection timing. Simultaneous USB/wireless standby, automatic arbitration and the Launcher V2 visual redesign are subsequent work; this change does not claim to implement them.

## Startup and lifetime

Call `carplay_connection_manager_start()` from the application startup owner. Initialization itself is not concurrent. Successful calls are idempotent. Mutex allocation, worker creation and worker startup failures return `RTCP_ENOMEM` or `RTCP_EIO`; a failed worker creation/startup can be retried without creating a second worker. The manager and its status mutex have application lifetime. This version does not expose a manager destruction API.

The existing GUI configuration starts the manager after creating the launch page. The manual Headless profile keeps its existing CLI receiver lifecycle. The manager source can be compiled independently of LVGL with or without wireless/audio-test support; that compilation is distinct from enabling automatic connection policy in the Headless firmware.

## Requests and snapshots

`carplay_connection_status()` copies the current connection stage, error, pairing challenge, endpoint and diagnostic counters under the manager mutex. It returns false if no manager exists or a nonblocking lock attempt fails. The caller owns the resulting value; subsequent worker updates do not change the copy. Transport, authentication and media readiness still have their separate SDK states; the application stage is a presentation summary.

Mode selection, retry, Bluetooth confirmation, pairing-file scan/delete and audio-test controls enter through manager functions. The worker retains ownership of session teardown, file I/O and sound-device test start/stop. File and audio pages reserve connection ownership before the worker processes them. Opening the file manager during wireless standby retires the borrowed iAP2/receiver state before accessing persistent records. Requests that violate the current stage or reservation are ignored, preserving the existing UI policy.

The file-list snapshot API copies entries into caller-provided storage and changes the revision only when a new list is available. UI button user data points into the UI-owned copy. Audio snapshots similarly copy selected settings and status. Audio test gain changes are posted to the manager and applied by the worker; no audio negotiation, decoding, mixing, microphone encryption or AEC behavior changes.

`carplay_connection_touch()` gates input against the manager's closing stage before calling the receiver's thread-safe queue. The receiver also excludes touch/Siri enqueue from start/stop to prevent access to a destroyed queue mutex. The launcher's own pointer mutex is independent of the manager mutex.

## Validation

Run from `packages/third-party/rt-carplay` with `SDK_ROOT` pointing to this checkout:

```sh
make check-lubanlite-demo SDK_ROOT=/path/to/luban-lite-carplay
make -j8 check
```

The application regression compiles the actual manager and launcher sources against SDK LVGL headers. It executes the actual worker with modeled USB, radio, media and scheduler services under ASan/UBSan. Coverage includes trust/storage failure, replug, network/first-frame wait, quiescent close, asynchronous Bluetooth failure, wireless mode switching, pairing manager exclusion, pointer routing/overflow and audio-test ownership. Added request-API checks cover startup failure/retry, idempotent startup, copied snapshots, file-list revisions and mutually exclusive management pages. Two additional syntax checks compile the actual manager without an LVGL include path, with audio/wireless enabled and disabled.

The SDK Make suite passes 58 test binaries plus the legacy-configuration encrypted end-to-end variant; CTest passes 59 cases, including the actual RT-Thread wrapper lifecycle regression. Host tests and syntax checks do not validate physical USB cancellation, Bluetooth reconnection, sound-device release or panel handoff. GUI/Headless cross-build results must be associated with the exact published source revisions. D213 validation of these changes is `NOT_RUN` until a matching image is flashed and tested.

## Receiver integration

The pinned SDK now accepts public `rtcp_receiver_config_t.platform`, `.media` and `.iap2` descriptors. The RT-Thread composition populates those directly, and its owner sends HID, Siri and tunneled iAP2 through public Receiver functions. Video remains Annex-B H.264; audio preserves LPCM/AAC/Opus format, stream-slot and timestamp metadata. The host still owns decoding/device workers, touch focus and UI handoff. This API migration does not complete the remaining board-adapter relocation or the singleton diagnostic ownership audit. See the SDK's [Receiver API](../../../../packages/third-party/rt-carplay/docs/receiver-api.md) and [lifecycle contract](../../../../packages/third-party/rt-carplay/docs/lifecycle-and-ownership.md).
