# Portable CarPlay V2.1 implementation progress

This is an intermediate implementation record, not a final acceptance report. The full V2.1 task remains open. Host tests, cross-builds and physical hardware validation are separate evidence categories.

## Starting revisions

The clean SDK submodule started at `ae3716513bd79d335250e7c51ee9897c37703453`; cherryusb-ipheth started at `f4716f1457145ba2d4aab755193645e1e1aed11d`. Current development uses rt-carplay main, cherryusb-ipheth main and the independent `boa-z/luban-lite-carplay` host's carplay-oss branch. No new development branch or PR is authorized. Original SDK sources were archived from Git into `/home/boa/carplay-v21-baseline/rt-carplay` for the pre-refactor Linux regression.

The first SDK implementation is committed as `ab7e72e29a14943a76b7d9af8603a5e1a2eee387` on main, with author and committer boa-z <boa-z@outlook.com>. This supersedes its earlier commit with incorrect identity. IPHETH documentation is committed as `2c87b69fda0d2dbeeb9df5180dd10157791d7371` on main with the same identity. The parent gitlinks are updated after verifying the remote branches resolve to these SHAs. Existing PRs were manually closed by the user; no further PR is authorized. The user subsequently authorized timely pushes of verified changes on the existing development branches.

## Implemented first stage

The SDK now has `include/rtcp/receiver.h` and `runtime/receiver.c`, providing real create/start/poll/stop/destroy operations over the existing RTSP implementation. Transport connection, ControlCipher authentication, RECORD acceptance, Screen endpoint, audio endpoint and encrypted input readiness are independent state fields. The runtime copies the initial protocol configuration and capability plist, supports failed-start retry and idempotent start/stop, rejects lifecycle reentrancy and supplies an intentional Null video sink. All operations belong to one host-selected owner thread.

The Linux simulator and transitional RT-Thread receiver use the new runtime. The old `carplay_app_*` API remains available; its declaration moved to `runtime/rtthread/carplay_app.h`, with a compatibility include at the old path. The runtime composition moved out of the example to `runtime/rtthread/`; board media, touch, pairing and transport helpers moved to `port/luban-lite/app/` without rewriting their implementations. The actual LVGL launcher moved, with unchanged source content, into this host at `ui/lvgl_launcher.c`.

The host now calls the root SDK SConscript. Production SDK source collection does not call example build scripts or compile an LVGL launcher. Group checks prevent compiling the same SDK twice when discovered through both package and application integration. The legacy example SConscript forwards to the production entries. `LPKG_RT_CARPLAY_RTTHREAD_RUNTIME` selects the transitional composition in application mode.

The additional `d21x_demo128-nand_rt-thread_rtcarplay_headless_defconfig` keeps the reference board and USB/radio protocol services while disabling LVGL, video, audio and touch. It uses the CLI lifecycle entry. The normal configuration and its built image were preserved while testing this profile; the normal `.config` was restored afterward.

## Executed verification

| Check | Result | Evidence |
| --- | --- | --- |
| Original SDK `make -j8 check`, ASan/UBSan | 56 test binaries and boundary check passed | `output/carplay-refactor-baseline/original-sdk-linux-check.log` |
| Modified SDK `make -j8 check`, ASan/UBSan | 57 test binaries and boundary check passed | `output/carplay-refactor-baseline/linux-make-check.log` |
| Modified SDK CMake/build/CTest | 57/57 passed | `output/carplay-refactor-baseline/linux-cmake-check.log` |
| New Receiver lifecycle | Real sockets, copied configuration, bind-failure retry, reentrant lifecycle rejection and 32 start/stop cycles passed | `test_receiver` in the SDK test logs |
| Encrypted end-to-end Receiver | Pair-Setup, Pair-Verify, control, audio, Screen, readiness separation and reconnect passed | `test_airplay_pairing_e2e` in the SDK test logs |
| IPHETH Linux Debug | 3/3 passed, including actual external CherryUSB class and upstream EP0 | `output/carplay-refactor-baseline/ipheth-linux.log` |
| IPHETH Linux ASan/UBSan | 3/3 passed | `output/carplay-refactor-baseline/ipheth-linux.log` |
| SDK USB DMA, actual video/queue and touch adapter regressions | Passed before the initial launcher-path test failure | `output/carplay-refactor-baseline/linux-adapters-check.log` |
| Actual host launcher, handoff, reconnect and capability regressions | Passed after fixing the host launcher include to an absolute path | `output/carplay-refactor-baseline/linux-owner-check.log` |
| D213 normal GUI profile | Recompiled, linked and packaged successfully | `output/carplay-refactor-baseline/production-entry-build.log` |
| D213 Headless profile | Recompiled, linked and packaged successfully | `output/carplay-refactor-baseline/headless-build.log` |
| Current refactor on a D213 board | NOT_RUN | No hardware acceptance is inferred from builds |

The CMake audio-sink test initially failed because its fixture path was relative to the build directory. Its CTest working directory now matches the source directory, as already required by the audio-engine test; audio behavior was not changed. Shell scripts now have explicit LF checkout attributes so a Windows checkout can run them in WSL. CMake's mbedTLS output follows the selected build directory, and the host builder notices changes to its own script/configuration.

The normal preserved image is 4,778,496 bytes with SHA256 `a24688ee22eb6174d75e82dcdcb091b67fb3dd1f3da42f5550553f426b24b9b3`, saved as `output/carplay-refactor-baseline/gui.img`. The Headless image is 3,983,872 bytes with SHA256 `5dca4ed60f0e4159ed518955343dcf2f0dec5241868681b22b30f78a5afaad8f`, under `output/d21x_demo128-nand_rt-thread_rtcarplay_headless/images/`. These artifacts were not flashed.

## Documentation delivered so far

The SDK now includes full English/Chinese pairs for `architecture.md`, `receiver-api.md`, `lifecycle-and-ownership.md` and `audio-debug-handoff.md`. Each pair has bidirectional language navigation and single-line prose paragraphs. The audio handoff records the frozen behavior, real code map, thread/device ownership, existing commands and zero-stream reproduction evidence. The existing README and integration reference still need the complete bilingual audit and translation required by V2.1.

IPHETH now has ten complete English/Chinese document pairs, including both READMEs, every pre-existing technical document and a bilingual audit. The seven original technical documents retain their normalized original text, apart from an explicit historical-evidence note. Windows and WSL documentation checks pass for all ten pairs, and both run the checker's seventeen isolated mutation tests successfully. Independent Debug and ASan/UBSan driver tests each pass 3/3. Its existing host workflow now includes a documentation job; remote execution of this new job is tracked separately from local validation. No driver source, public C interface or original legal notice changed.

## Remaining required work

The application Connection Manager has now been extracted into `app/connection_manager.c`, with LVGL using request and snapshot APIs. Its actual-source regression and two compilation checks without LVGL headers pass. See [Application connection manager](connection-manager.md) for contracts and evidence. Simultaneous USB/wireless standby and Launcher V2 remain subsequent work. The RT-Thread wrapper now checks scheduling failure, serializes lifecycle/input entry and publishes completion after teardown; both SDK Host suites pass 58 tests. These updates do not complete the remaining ownership audit or physical validation.

- Replace the low-level `airplay_rtsp_server_config_t` migration bridge with the final public host/media configuration and consolidate `include/rtcp/media.h` without dropping encoded-audio metadata.
- Validate the extracted Connection Manager in exact-revision GUI and Headless integration builds, retaining its tested mode switching, first-frame gating, diagnostics and local audio controls.
- Complete RT-Thread legacy singleton/worker lifetime hardening; its old cross-thread diagnostic APIs are not covered by the portable Receiver owner-thread guarantee.
- Move final board/backend ownership into Luban-Lite, register replaceable media/input/platform backends and move UI-layer policy out of the video sink. The transitional board code currently remains inside the SDK port.
- Consolidate Kconfig symbol ownership and add the standalone generic RT-Thread Headless example. The D213 Headless profile is an integration build, not proof that every SDK adapter is generic RT-Thread.
- Complete every maintained English/Chinese SDK document pair, reflow existing Markdown and add document/relative-link/language-pair checks to CI. The independent IPHETH documentation stage is complete locally; preserve research, performance, historical validation and licensing facts in subsequent changes.
- Finish source review, reproducible reports and committed/fetchable cross-repository references. Do not create a PR or update a parent gitlink to an unpublished commit. Repository recreation to remove old closed PR records has not been performed; the earlier credential-scope limitation no longer applies. Refresh and verify backups before any authorized destructive operation. Final history cleanup is deferred until complete refactoring and validation.
- Execute or explicitly retain NOT_RUN for physical USB/radio reconnect, pair persistence, failure rollback, first displayed frame/HMI restoration, device open/close, microphone, touch ownership and USB/IPHETH hardware lifecycle.

The audio routing symptom, zero Audio SETUP streams and Direct/Mixer/negotiation investigations remain a separate follow-up task. This stage adds no Android Auto, CarLife or CarLink logic and changes no audio negotiation, decoder, jitter, mixer, resampling, microphone encryption or AEC policy.
