[English](repository-migration-verification.md) | [简体中文](zh-CN/repository-migration-verification.md)

# Repository migration and baremetal verification

## Repository identity

The integration is maintained in `boa-z/luban-lite-carplay`, default branch `carplay-oss`, with local checkout `C:/Project/Carplay/luban-lite-carplay`. Its upstream remote is `https://github.com/boa-z/luban-lite.git`, branch `master`, snapshot `4060f85bd27a1eb555952921139b77cc41bdeb4e`. This is an independent repository, not a fork of `jiachenin/luban-lite-jc`; the published ancestry contains the upstream snapshot followed by the CarPlay integration commits. Other products' development commits were not imported. The former shared worktree is retained as a local migration backup.

The migration commit is `f0ceb6b6`, followed by `38217874945d6a3035293cc6831257b53d67bd92` for upgrade descriptor/restart regression coverage. Both author and committer are `boa-z <boa-z@outlook.com>`. The published submodule pins are:

| Component | Commit |
| --- | --- |
| rt-carplay | `741056aba0d6299c5fa1a5e3c9ca1974f37e14a2` |
| cherryusb-ipheth | `2c87b69fda0d2dbeeb9df5180dd10157791d7371` |

No pull requests were created for this migration.

## Software verification

The WSL host build of rt-carplay at the pinned commit passed all 57 `make -j8 check` test executables and the dependency-boundary check. The CherryUSB overlay's actual-source tests passed with ASan/UBSan: baremetal semaphores, queue capacity/wrap, IRQ completion and timeout, timer cancellation/self-deletion, Hub connect/disconnect/reconnect, initialization failure rollback, orderly teardown, typed upgrade descriptors, non-null event callbacks and repeated device initialization/deinitialization. Existing EHCI short/zero/full/error-chain, cache hazard, diagnostic snapshot and EP0 shared-lock timing tests also passed.

GitHub Actions [run 43618003385](https://github.com/boa-z/luban-lite-carplay/actions/runs/43618003385) completed successfully for both GUI and Headless at SDK commit `38217874945d6a3035293cc6831257b53d67bd92`. Each job built the Bootloader from this repository, checked the packaged binary against that build, then built and packaged the RT-Thread application. All artifact SHA256 entries were independently verified after download. Both IMG containers contain nine records whose component bounds and CRC32 values passed verification, including the DDR updater, Bootloader and OS. The integrity checker also rejected corrupted OS data, truncation and a missing OS record.

| Image | Bytes | SHA256 |
| --- | --- | --- |
| GUI | 4,778,496 | `cea07af4ec8a17bd2e610dee45e45d77c295eca0e4aa749e4ce4d059b1ffa20a` |
| Headless | 3,983,872 | `4061051253052f9246be97e9148c7ac851e4c945fa35f295252a252119035415` |
| GUI Bootloader | 363,560 | `fccc6de0361e3dfb6cb74cd08bdddf7ac40e8d6422867aa47b81971562265269` |
| Headless Bootloader | 363,496 | `27f39ce5fb24dfea4e6f4dc5316fa9914bc69568f96c16f9581bde68e0075f82` |

## Board feedback

On 2026-11-14 the user reported flashing artifact `d213-carplay-gui-38217874945d6a3035293cc6831257b53d67bd92` and successfully booting the board, then explicitly corrected the initial functionality report: CarPlay connects but its picture is not displayed correctly. Record **USER_REPORTED_PASS** for flashing/boot only and **USER_REPORTED_FAIL** for CarPlay video display. The artifact has not passed end-to-end CarPlay acceptance. This task did not collect serial logs, a step-by-step test matrix or a separate board-side hash readback. See the [display regression investigation](video-handoff-fix.md).

Headless board operation, individual U-disk/SD-card upgrade cases, OTG role switching and extended reconnect/stress tests remain **NOT_RUN / not individually reported**. The user's feedback does not establish that every one of these paths was exercised. Later source commits must retain their own build and board status; they do not inherit the board acceptance of `38217874`.

## Remaining V2.1 scope

The later [V2.1 verification handoff](refactoring-v21-verification.md) supersedes the intermediate migration status and records the implemented architecture, lifecycle audit, bilingual documentation and exact-image software verification. Physical acceptance remains NOT_RUN. Audio negotiation and routing behavior remains frozen for the separate audio-debug handoff.
