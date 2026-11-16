[English](d213-ultra-feasibility.md) | [简体中文](d213-ultra-feasibility.zh-CN.md)

# D213 codec and graphics feasibility for official Ultra

Current scope update (2026-11-15): complete official Ultra is no longer required in this development environment. This report preserves the technical limits for a possible future official product; its missing codec, graphics, asset and qualification capabilities are not acceptance blockers for the current portable protocol foundations and D213 fusion demo. See the [current scope and acceptance record](ultra-development.md#current-scope-and-acceptance).

Source review date: 2026-11-15. The current D213 development stack can host portable protocol experiments and the independent Classic video plus local instruments demo. The checked-in codec and graphics interfaces do not establish the complete official Ultra rendering pipeline. Implementing more negotiation messages cannot by itself close those gaps.

## Public architecture requirements

Apple's [WWDC 2024 architecture presentation](https://developer.apple.com/videos/play/wwdc2024/10111/) describes HEVC video streaming as essential, a wireless-only experience, an OpenGL-based Local UI renderer using images and behavior scripts, and an OpenGL compositor combining Remote UI, Local UI and Punch-through UI before the system compositor. The public transcript was retrieved again on the review date. This is public architecture evidence, not the complete partner specification or an authorization/qualification result.

The application's existing hardware-layer fusion remains useful: it combines decoded CarPlay video and a separate LVGL dashboard on the same physical screen. It does not execute the official asset/script package or reproduce the described renderer and presentation synchronization contract.

## Checked-in implementation evidence

| Area | Source evidence | Consequence |
| --- | --- | --- |
| Codec identifiers | [mpp_dec_type.h](../../../../packages/artinchip/mpp/include/mpp_dec_type.h) defines H264, MJPEG, PNG and AICP decoder identifiers. | There is no HEVC decoder identifier in this API. An identifier alone would still not implement a decoder. |
| Decoder dispatch | [mpp_decoder.c](../../../../packages/artinchip/mpp/ve/decoder/mpp_decoder.c) dispatches JPEG, conditional H.264, PNG and conditional AICP; unsupported types return NULL. | The actual factory has no HEVC implementation or dispatch branch. |
| Build selection | [MPP Kconfig](../../../../packages/artinchip/mpp/Kconfig) selects the H.264 decoder with its VE version dependency. [MPP SConscript](../../../../packages/artinchip/mpp/SConscript) collects JPEG, PNG, zlib and conditional H.264 sources. | Changing the Ultra demo flag does not add a HEVC implementation to the image. |
| Graphics accelerator | [mpp_ge.h](../../../../packages/artinchip/mpp/include/mpp_ge.h) exposes fill, bitblt, rotation, command emission and synchronization. | These operations support application graphics. They do not establish an OpenGL context, shader pipeline or the official Local UI runtime. |
| Single-screen application | [video_layer_policy.c](../ui/video_layer_policy.c) and [fusion_display.c](../app/fusion_display.c) manage the DE/LVGL split, video placement, mode transitions and input ownership. | Their tested application contract remains distinct from official renderer/asset/presentation acceptance. |

A scoped source search of ArtInChip MPP/BSP, build configuration and the D21x target found no HEVC decoder or board OpenGL/EGL integration entry. ThorVG headers under the bundled LVGL tree contain generic OpenGL engine declarations, including unsupported-engine comments; those declarations do not establish a working D213 graphics driver. This is an observation about this checkout and selected integration, not a claim about every vendor SDK or the chip's silicon capabilities.

## Evidence still required

| Question | Current result | Required evidence |
| --- | --- | --- |
| Can the current MPP factory decode HEVC? | No implementation in the reviewed factory/build path. | A real decoder backend, supported profile/level/chroma/bit-depth limits and error/lifetime handling before any HEVC declaration. |
| Can D213 silicon accelerate HEVC or expose suitable OpenGL support through another supported vendor stack? | UNKNOWN. | Authoritative chip/vendor documentation and a supported driver/runtime, followed by actual device measurements. |
| Could a software codec/renderer meet the target? | NOT_RUN. | A maintained portable implementation, licensing review, memory budget and measurements on D213 with wireless, decoding, rendering and audio active together. CPU feasibility cannot be inferred from successful Host tests. |
| Does DE/LVGL fusion satisfy official Local UI? | Not established. | Authorized package validation/execution, renderer behavior, layer semantics, timestamps and presentation synchronization against the applicable integration contract. |
| Is complete official product integration/qualification available? | Unverified. | The applicable official access, integration requirements and qualification evidence. Open-source protocol experiments cannot substitute for that process. |

Keep unfinished phone-facing declarations disabled. Continue the platform-independent Vehicle State/UI Sync foundation and real-iPhone protocol experiments under their own acceptance levels. Before treating D213 as the final official Ultra product platform, resolve the codec and renderer questions above. A second logical stream or a matching dashboard appearance cannot settle them.

## Acceptance boundary

This review contributes to L0 source research only. L1 covers the separately recorded Host protocol tests. The [first fusion flash record](evidence/d213-fusion-first-flash-20261115.md) identifies the board image and restored identities; full L2 physical composition/input acceptance remains NOT_RUN. L3 real-iPhone Ultra negotiation, L4 real-iPhone Vehicle State/UI Sync and L5 independent Type 111 remain NOT_RUN. No new flash, codec benchmark, graphics-driver test or phone exchange was performed for this review.
