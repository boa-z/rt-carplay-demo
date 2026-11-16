# D213 fusion viewport: manual-test image

[简体中文](d213-fusion-viewport-20261115.zh-CN.md)

> Historical record: the profiles and script named below were removed when the D213 configurations were unified into `d21x_demo128-nand_rt-thread_rtcarplay_defconfig`. The identities still identify the artifacts built at the time; they are not the current build command.

## Change and artifact identity

The Ultra GUI profile advertises Main Display at 768x600 while the physical panel, LVGL framebuffer and raw launcher input remain 1024x600. The previous 1024x600 stream fitted into the left 768x600 viewport produced a 768x450 image and 75-pixel top/bottom bars. A phone stream rendered at the new advertised size fits that viewport without cropping or stretching. iPhone output and visual/input acceptance remain NOT_RUN for this image.

| Item | Evidence |
| --- | --- |
| Integration revision | `8ac6f5044df68073c19cf1d3b7d88b0a5775778a` |
| SDK revision | `0692be508dfd84a1adb170f6bcbb390740a30963` |
| Configuration | `d21x_demo128-nand_rt-thread_rtcarplay_ultra_gui_defconfig` |
| Build | Native Windows, `.ci/build-ultra-demo.sh gui`, exit 0 |
| Image | `d213-carplay-ultra-gui.img` |
| Native image SHA256 | `0c32245094b143f99838c36e5cbc90d0d17e04aefb877e5561f7988c57e5dbca` |
| Local verification | Exact source pins, clean integration/SDK inventories, six SHA256 entries, nine component bounds/CRC32 checks and config/ELF wired-profile checks passed. Generated config confirms logical 768x600 and physical 1024x600. |
| Host | Full application `make -j8 check` passed; actual MPP sink fixture starts with logical 768x600 and discovers physical 1024x600, latches the full left viewport, preserves scanout ownership and verifies FULLSCREEN side bars. Coordinator tests cover phone corner coordinates and the right local-region boundary. |
| CI | [43786904059](https://github.com/boa-z/luban-lite-carplay/actions/runs/43786904059): Host, GUI and Headless passed. Both firmware artifacts downloaded and passed exact pins, clean inventories, six SHA256 checks and nine component bounds/CRC32 checks; GUI advertises width 768 and Headless retains 1024. |

The artifact is preserved outside Git under `C:/Project/Carplay/rt-carplay-artifacts/ultra-research/native-8ac6f504/gui/`. The Host and build logs are `fusion-viewport-host.log` and `fusion-viewport-native-gui.log` in the containing research directory. CI images are separate artifacts and need not have the native image hash.

CI GUI SHA256 is `6ffb03be1d3685c3e759a435f75d0f9cbceb5c182e7901f82024aa99ee6d5497`; CI Headless SHA256 is `947bbbf6c21cd348a87fc40437953a4700b9bf43905ffe91a2d7f11381e98d91`. These downloaded artifacts are under `C:/Project/Carplay/rt-carplay-artifacts/ultra-research/ci-8ac6f504/`. Neither is recorded as flashed.

## Manual validation

The user owns flashing and testing and explicitly requested that the agent stop connecting to the board. No flash or new-image device acceptance is claimed. Pairing must be backed up before manual flashing and restored/read back afterward; the attempted exports in this checkpoint failed because ADB found no device and are not valid backups. Earlier verified backups remain private, but may predate subsequent phone trust updates.

Re-establish the CarPlay session after flashing. Check `carplay_ota status` for `8ac6f504`, then `carplay_display status` for logical 768x600 and a video rectangle of (0,0) 768x600 with error zero. Confirm the visible image has no top/bottom bars, all content is present, corner/edge phone controls respond correctly and the right 256x600 instruments remain usable. Check LOCAL while unplugged and reconnect back to FUSION. A different phone output aspect ratio can still cause bars; counters alone do not prove visual success.

FULLSCREEN during the same 768x600 session keeps its aspect ratio and has 128-pixel side bars. Mode buttons do not renegotiate the phone resolution. Ordinary Classic and Headless profiles retain their previous 1024x600 size. Dynamic session reconfiguration remains unimplemented.

## Earlier fixed-alpha image and acceptance boundary

The preceding native `1e215116ca5dde09a1d276ca45395eba321e6e12` GUI image had SHA256 `0ac41a8c114617a2f991f715a42eda31eda175ebfc1fbb53d320b64bb0af8537`. It was flashed after two matching six-file pairing exports; all six files matched a readback after restoration. After restart, a later export contained seven files, with updated trust/pairing contents, so it must not be described as identical to the six-file export. Runtime reported firmware 1e215116, FUSION video at (0,75) 768x450, logical 1024x600 and pixel alpha mode 0/value 255 without an override. The user's subsequent report of visible video with top/bottom bars motivated this viewport change. Full phone/local touch, reconnect and fault acceptance were still incomplete.

The independent preceding [CI 43776008538](https://github.com/boa-z/luban-lite-carplay/actions/runs/43776008538) passed Host, GUI and Headless. Its GUI hash was `1f3e28188a2d0c6e97f7c42a054b3c0d7e00920634b75e98e4aa8bc0c64556d8`, and Headless hash was `d8d67778f942ce05ee5b362f29a021b183722c677bd62300901308a604da17f0`; neither CI image was flashed. Keep these identities distinct from the native image.

L0 remains PASS; L1 and the earlier L2-L4 observations remain PARTIAL; real-phone L5 remains NOT_RUN. New-image L2 viewport acceptance is NOT_RUN pending the user's manual results. This image does not establish official Ultra support and does not enable UI Sync, HEVC or Type 111.
