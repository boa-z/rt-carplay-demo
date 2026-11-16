# D213 Mobility Cockpit V4 native UI — Host evidence

[English](d213-mobility-cockpit-v4-20261116.md) | [简体中文](d213-mobility-cockpit-v4-20261116.zh-CN.md)

The user asked to rebuild the demo UI in place against the locked V4 design package, keeping media volume, display brightness, Chinese/English, theme and the FULLSCREEN/FUSION CarPlay display choice, and to clean up the retired test pages. This checkpoint records what the source tree proves on a host and what still needs the board. Nothing here is a board or protocol acceptance result.

## Source state

| Item | Value |
| --- | --- |
| Integration branch | `carplay-oss` |
| Base revision before the V4 work | `2917135e666025d315e6cc069734e9776df39024` |
| Changed/new files | 49 paths (see the change report in the commit message and `git show --stat`) |
| Image SHA256 (working tree) | `f776ab8a7fc548195fd71b593b51881254b1df081042e059f8bd64ddc03a7b1b` |
| Portable SDK | Unmodified `packages/third-party/rt-carplay`; no LVGL, HMI, Android Auto or CarLife dependency added |
| New application or defconfig | None. The existing `rtcarplay` app and its two defconfigs were edited in place |

## Build

`scons -j8` with `d21x_demo128-nand_rt-thread_rtcarplay_defconfig` completed with exit 0 and produced `output/d21x_demo128-nand_rt-thread_rtcarplay/images/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img` (5,276,160 bytes at this revision; the SHA256 above identifies that exact artifact). The active configuration contains `AIC_USING_PWM3`, `AIC_PWM_BACKLIGHT`, channel 3 at 1 kHz, `AIC_PWM_BRIGHTNESS_LEVEL=72`, and no `AIC_PANEL_ENABLE_GPIO`; `AIC_SDMC1_HOTPLUG_PIN` remains `PC.6`.

## Host tests

| Check | Result | Notes |
| --- | --- | --- |
| `check-lubanlite-demo` (`test_lubanlite_demo`) | PASS | Scenario rewritten for the retired audio page: an open manager excludes the public requests, a bare idle pass consumes nothing, a queued retry is consumed without restarting the receiver, and a close returns to idle so the next open succeeds |
| `check-auto-connections` (headless, GUI, fusion) | PASS | 32-cycle discovery/failure/recovery and native 768→1024→768 session changes |
| `check-lubanlite-video`, `check-fusion-display` | PASS | Host video policy and single-screen modes |
| `check-lubanlite-touch` | PASS | Ownership, IRQ, coordinates, 32 restarts |
| `launcher_preview` | PASS | Renders all four pages, modals and the unsupported-audio state at 1024×600 with real LVGL 9.1 |
| `fusion_preview` | PASS | Seeded projection-side bytes unchanged; every strip widget satisfies `x >= 768`; touch at 767/768 splits correctly |
| `check-internet-share`, `check-lubanlite-{bonds,handoff,reconnect,capabilities}`, `check-wired-{identity,trace}`, `check-iap2-endpoints`, `check-{usb,wireless}-iap2-worker` | NOT RUN (environment) | These targets need Linux host facilities this Windows session does not provide: `-fsanitize=address,undefined` runtime libraries, POSIX `sys/socket.h`/`O_CLOEXEC`, or a bare `cc` in `PATH`. They are untouched by this change and must run in the Linux CI/`make check` environment. |
| `check-udp-burst` | NOT RUN (environment) | `tests/udp_burst/run.sh` invokes `cc` |

None of the NOT RUN failures involve a file changed here; the same targets fail identically on the base revision in this shell.

## Screenshots

`tests/launcher_preview/shots-v4/` — 15 images: dashboard, connectivity, settings (including the no-audio disabled state), OTA (not checked, available, confirm, applying) and pairing, in dark/light and en/zh combinations produced by the test flow.

`tests/launcher_preview/shots-v4-fusion/` — 8 images: the 256×600 strip with provider data, unavailable data, light and Chinese variants, disconnect and FULLSCREEN-hidden.

All are 1024×600 PNG rendered from the actual UI sources and actual LVGL by the host harness. Connection, OTA and vehicle values are modeled. They are not phone-stream captures, board screenshots or proof of actuation.

## ACCEPTANCE.md mapping

| # | Requirement | Result | Where |
| --- | --- | --- | --- |
| 1 | Full UI compiles with LVGL 9.1 and the D213 BSP; host screenshots at 1024×600 | PASS (Host) | `scons -j8` exit 0; `launcher_preview` and `fusion_preview` render 1024×600 |
| 2 | Only four native screens; no extra data/signal-failure/diagnostics pages | PASS | `PAGE_DASHBOARD/CONNECTIVITY/SETTINGS/OTA` only; retired pages deleted |
| 3 | Speed and battery are the visual focus on the dashboard and fusion strip; no mandatory extra signals | PASS | 220 px speed and 110 px SOC on the dashboard; 115 px/58 px in the strip |
| 4 | Dark/light render all four pages and the strip; immediate, persisted, no session restart | PASS (Host) | `select_theme` restyles the tree and writes `CARPLAY_PREF_THEME`; shots in both themes |
| 5 | zh/en updates every native label, persists, no missing glyphs | PASS (Host) | `ui_language.c` tables; all four CJK sizes regenerated from one union set; `find_caption` assertions in zh |
| 6 | Volume 0/25/60/100 reaches the real backend with read-back; disabled without a backend | PASS (Host model), board NOT_RUN | Slider events drive `carplay_volume_set()`; no-audio state renders disabled |
| 7 | Brightness 5/25/75/100 invokes the driver safely; persists | PASS (Host model), board NOT_RUN | Range 5..100; `carplay_backlight_set()` to PWM3; `CARPLAY_BRIGHTNESS_MIN` floor |
| 8 | Mode stored; FULLSCREEN zero LVGL pixels and correct touch; FUSION nothing at x<768; touch 767/768 | PASS (Host) | `fusion_preview` byte-identity + widget-bounds + split-touch assertions |
| 9 | Mode switch during a connection either renegotiates or says reconnect needed | PASS (Host), phone NOT_RUN | `carplay_connection_set_display_mode()` rebuilds the session; refusals surface as status text |
| 10 | Connectivity reflects real progress/failures; Android Auto/CarLife+ do not pretend to work | PASS | Stage strings from the manager; "Planned" with no backend |
| 11 | OTA shows real version/availability/lifecycle/progress/failures; disables unavailable backends; no UI-only success | PASS (Host), board NOT_RUN | `carplay_ota_*` only; disabled without service/network |
| 12 | No FPS-heavy decorations; screenshots for dark/light and zh/en for each page and fusion | PASS (Host) | 23 checked-in renders; labels update only on change |
| 13 | Tests/benchmarks and a brief report of limitations | PASS | This checkpoint plus the V4 doc's limitation sections |
| 14 | No branch/PR/push or unrelated protocol core change without authorization | Followed | Work stayed on `carplay-oss`; `packages/third-party/rt-carplay` is untouched |

## Hardware boundary

NOT_RUN on the board for this revision: flashing the image; PWM3 backlight sweep over 5/25/75/100 including the visible floor; `sound0` mixer volume at 0/25/60/100 with audible result; light/dark and zh/en persistence across reboot; FULLSCREEN zero-LVGL-pixel behavior over live video; FUSION left-side cleanliness and the x=767/x=768 touch split on the panel; display-mode renegotiation with a real iPhone; and the OTA check/download/install/reboot path against the published preview release.

The schematic confirms PC.7 is `PWM3_B` feeding the LED boost, and PC.6 is `SDC1_DET`. Whether that PWM reaches the boost enable in every panel variant, and whether the codec mixer answers the capability query on this image, can only be confirmed on hardware. If the mixer is absent the volume slider is disabled rather than lying; if the PWM device is absent the brightness slider is disabled the same way.
