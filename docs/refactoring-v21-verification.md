[English](refactoring-v21-verification.md) | [简体中文](zh-CN/refactoring-v21-verification.md)

# V2.1 architecture and verification handoff

## Acceptance boundary

The portable SDK refactor, host ownership migration, lifecycle corrections, independent Headless example and bilingual documentation are implemented and software-verified. This is the prerequisite baseline for subsequent automatic connection and Launcher V2 development. Physical acceptance is NOT_RUN. The previous board report for 38217874 established boot only and reported incorrect CarPlay display; it does not validate this baseline. No PR was created. Development and pushes use the existing host carplay-oss and SDK main branches with boa-z author and committer identity.

## Architecture and interfaces

| Owner | Implemented responsibility |
| --- | --- |
| SDK include/rtcp | Public Receiver configuration, platform/media/iAP2 descriptors, events, input, errors and copied diagnostics |
| SDK core and runtime | Portable protocol and poll-driven Receiver lifecycle; no GUI or SoC dependency |
| SDK port/linux and port/rtthread | Generic OS and network adapters |
| SDK examples/rtthread_headless | Standalone public-API Receiver with Null media; not a board application |
| Host app | Receiver composition, Connection Manager, USB/iAP2 ownership, pairing persistence and queued commands |
| Host platform | Realtek/Zephyr/MFi/CherryUSB, MPP/DE, sound output/capture and GT911 |
| Host ui | LVGL and framebuffer visibility policy; connection requests and copied snapshots |
| Independent cherryusb-ipheth | Apple Ethernet protocol/class/network lifecycle; no dependency on the CarPlay SDK |

Receiver create/start/poll/get_state/stop/destroy run on one host owner. Its public state distinguishes transport, authentication, session, video, audio and encrypted input readiness. Encoded media callbacks borrow buffers until return; asynchronous backends copy them. Platform tables and provider contexts remain valid until teardown completes. Video readiness means a protocol endpoint; Launcher handoff requires an actually displayed frame. The host retains safety/HMI layer policy. See the SDK Receiver, lifecycle and migration documents and [host snapshots](session-snapshots.md).

The host uses the SDK root SConscript directly. SDK production builds do not compile board examples. Kconfig symbols have one owner; generic SDK and board options are separated. Protocol sources and external drivers are not duplicated. The former board carplay_app API now belongs to host app/carplay_app.h; obsolete SDK example entries produce explicit migration errors. Receiver keeps its optional legacy configuration converter. Shell changes to Control, framing, HID and Siri are queued for the poll owner; cross-thread queries return copies.

## Lifecycle review

| Resource | Implemented lifetime and regression |
| --- | --- |
| Receiver and host singleton | Start failure rollback, repeated start/stop, owner reentry exclusion, copied state, reader retirement and queued commands |
| Wireless iAP2 | Serialized lifecycle, cancellation generation, retained endpoint/provider storage, self-join rejection and late cleanup |
| USB/iAP2 | Command gate, detach generation, provider ownership, stream retained through timeout and retry |
| Touch | Reader retirement, failed-stop retry, callback lifetime and application input ownership |
| Audio output | Counted readers, serialized lifecycle, 5 s timeout retaining all output resources, self-join rejection and repeated stop cleanup |
| Video output | Counted readers, serialized observer/lifecycle, 3 s timeout retaining decoder/display state, callback exclusion and repeated stop cleanup |
| Microphone | Startup cancellation generation, serialized start/stop, 5 s timeout retaining borrowed network context until owner exit |
| Offline audio test | Connection Manager is its lifecycle owner; receiver is stopped first, and restart waits for successful test stop |
| Pairing and files | Manager pauses connection owners before deletion; identity and incompatible records preserved |
| IPHETH | Independent class/EP0 regressions cover reconnect, cancellation, interface and network lifetime models |

Audio decoding, negotiation, jitter, Mixer/Direct, resampling, encrypted microphone uplink and AEC policy are frozen. The audio/video worker data paths were compared against their preceding revisions; lifecycle changes surround them. Audio routing and zero-stream symptoms are handed off independently in the SDK audio-debug-handoff pair.

## Source and implementation records

| Repository | Published revision and role |
| --- | --- |
| luban-lite-carplay | f2902877e7efd6a7dbbca4da2445cd50cbf1e8c0, final executable lifecycle baseline |
| rt-carplay | 8947f76297f659d00bb0b32a5d202b86a91f86b6, complete 26-pair documentation and tested SDK baseline |
| rt-carplay | 68158e3, documentation-only reconciliation of completed host ownership and test counts |
| cherryusb-ipheth | 2c87b69fda0d2dbeeb9df5180dd10157791d7371, independent driver and 10-pair documentation baseline |

Host lifecycle commits include c31cac8a (touch), f1c14aa9 (audio output), 3a0fbabe (video), and f2902877 (microphone). The SDK moved 36 board implementation files and ten board-specific tests into the host without rewriting their media/protocol behavior. Later changes address explicit ownership defects with actual-source regressions. Use Git name-status and rename detection between the recorded starting and final revisions for the complete added/moved/deleted inventory; [the intermediate record](refactoring-v21-progress.md) preserves the migration sequence and original evidence. Submodules remain pinned to committed, remotely fetchable revisions. No private authentication material was added.

## Executed software verification

| Check | Result and evidence |
| --- | --- |
| SDK Make | ASan/UBSan suite, dependency boundary, public C99/C++11 headers and documentation PASS |
| SDK CMake/CTest | ASan/UBSan 55/55 PASS; 54 executables and encrypted legacy variant |
| SDK CI | [43642152335](https://github.com/boa-z/rt-carplay/actions/runs/43642152335) PASS at 8947f762 |
| IPHETH | Independent external CherryUSB class/EP0 Debug and sanitizer tests 3/3 PASS; docs and mutation checks PASS |
| Host Make | Full actual composition, media, persistence, manager, USB/radio and lifecycle ASan/UBSan suite PASS |
| Host CI | [43644729058](https://github.com/boa-z/luban-lite-carplay/actions/runs/43644729058) Host, GUI and Headless PASS at f2902877 |
| GUI/Headless | Complete images with same-tree matching baremetal Bootloader; all five SHA256 manifest entries per variant and all nine IMG component bounds/CRC32 independently checked after download |
| Generic Headless example | COMPILED_NOT_STARTED in firmware integration; standalone behavior tested with Host scheduling model |
| D213 hardware | NOT_RUN for this source/image; no inferred display, audio, reconnect or DMA acceptance |

| Baseline image | Bytes | SHA256 |
| --- | --- | --- |
| GUI | 4788736 | 87ddb69931c278ecc37d887666e412acb67e7414e1f4283bb500eb2b87f981d6 |
| Headless | 3987968 | 9b2a5762b6670d862837e5eb2876e002cae1744994b4198f6f68ba9d9c17b8a2 |

These image identities apply exactly to f2902877 with SDK 8947f762. Later documentation or Launcher changes require their own CI and image identity. Local raw logs, behavior-freeze comparisons and downloaded manifests are retained under ignored output/ci-logs and output/ci-verified-43644729058. Reproduce with SDK make check and sanitized CMake/CTest, independent IPHETH CMake/CTest, host make -C application/rt-thread/rtcarplay -j8 check, and the GUI/Headless workflow commands in [CI](ci.md).

## Bilingual documentation audit

SDK has 26 complete English/Chinese pairs covering the README, architecture, Receiver API, lifecycle, host migration, audio subsystem and handoff, existing audio research, USB authentication/persistence/video investigations, wireless/bonds, performance, RT-Thread ports/examples and audio fixture/dependency attribution. IPHETH has ten complete pairs: root README, examples README, architecture, compatibility, latency, porting, protocol audit, sources, validation and documentation audit. Each repository's checker discovers maintained files, verifies naming, reciprocal navigation, local links/anchors, language, single-line prose, section structure, fenced examples, inline identifiers and external-reference parity. Each checker passes 17 isolated mutation tests and runs in CI.

The SDK exempts only third_party/ed25519/UPSTREAM.md as preserved upstream provenance. Original licenses and third-party legal notices are retained. IPHETH has no maintained-document exemption. Translation review preserves historical status and original commands/code/measurements; mechanical parity checks are not proof of semantic equivalence. The SDK checker prints the maintained pair count; Git lists the exact current inventory. [English/Chinese SDK documentation](../../../../packages/third-party/rt-carplay/README.md) and the independent IPHETH README provide reciprocal navigation.

## Remaining validation and follow-up

Physical USB/radio arbitration, trust/bond persistence, reconnect, first visible frame and Launcher restoration, sound0/dmic0, touch, failure rollback and long-running stress remain NOT_RUN. The previous framebuffer ioctl ABI defect has an actual-source regression and software fix; visible panel behavior needs a fresh board test. ASan/UBSan and modeled scheduling do not prove freedom from every hardware or cross-thread race.

Automatic simultaneous discovery, single active connection policy and Launcher V2 are subsequent application changes. They must preserve these SDK/driver boundaries and audio behavior. Authorized historical branch/repository cleanup is a separate administrative action requiring refreshed verified backups; it has not been performed by this handoff. No PR, branch creation or branch switching is part of this delivery.

## Repository cleanup after verification

At the owner's explicit request, boa-z/rt-carplay was re-created after V2 software verification to remove the two closed PR records. Repository id changed from 1450313589 to 1458244070; all-state PR count is zero. Its existing main branch now starts at 57936326c311850e006310e94d2aa9d711ba6a1e with boa-z <boa-z@outlook.com> author and committer. Code and tests are byte-identical to 68158e3; only both READMEs add history/evidence notes. The new SDK [CI 43648984405](https://github.com/boa-z/rt-carplay/actions/runs/43648984405) passes independently. Earlier SDK commit/CI links in this historical report may no longer resolve; complete Git bundles, a restored/fsck-checked mirror, PR metadata and final CI ZIP logs are retained in the owner's offline backup. Older host gitlinks require that backup. Current host development remains on carplay-oss, SDK on main, and no additional development branches or PRs were created.
