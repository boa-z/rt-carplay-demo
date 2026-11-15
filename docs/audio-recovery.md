[English](audio-recovery.md) | [简体中文](zh-CN/audio-recovery.md)

# Call echo and music playback recovery

This document records the earlier audio-repair baseline. The user subsequently reported persistent music faults and call echo; its Host PASS results do not establish board acceptance. See [media and transport repair](media-transport-repair.md) for the replacement implementation, automatic delay alignment and the new validation plan.

## Scope and evidence

The user reports normal wireless call/Siri playback, poor music playback, and the remote caller hearing an echo. These are separate downstream media and upstream acoustic symptoms. The previous GUI profile enabled microphone capture but disabled AEC. The D213 HAL samplerate setter changes playback and microphone clocks together. The previous sink discarded a complete decoded packet whenever any part overlapped the expected RTP timeline, and its media clock correction had no target fill. These facts are verified in source; no phone capture establishes a single cause for the reported music distortion.

This change stays in the Luban-Lite application and existing GUI defconfig. The portable SDK, protocol capabilities, codec implementations and submodule revisions are unchanged. The implementation is based on commit 838eef85. Ethernet Internet sharing and the verified PHY clock configuration remain enabled.

## Call echo

The GUI profile enables the existing fixed-point SpeexDSP AEC. The playback worker supplies the post-mix/gain stereo PCM and queries the existing sound0 queued-byte API. The reference history estimates DAC presentation times from queued samples, preserves contiguous 10 ms chunks and supports sub-frame reads across chunk boundaries. The exclusive dmic0 adapter timestamps DMA arrivals and subtracts capture backlog, so a delayed capture worker does not align against its current wall-clock time. Missing reference samples bypass cancellation and increment diagnostics rather than substituting an arbitrary old frame.

The microphone worker owns AEC state. Runtime mode or delay changes and capture discontinuities recreate the filter without changing UDP sequence or encryption state. Noise suppression stays enabled; the extra RMS AGC is disabled while AEC is requested to avoid amplifying residual echo. The default residual delay is zero after playback/capture queue compensation. It is not an installed acoustic calibration. The existing adaptive filter covers approximately 100 ms. The capture pipe becomes 4096 bytes, also changing the SDK's capture DMA period to approximately 21.3 ms at stereo 48 kHz; playback pool count becomes four blocks. These bounded buffers trade a little latency for scheduling headroom.

```text
carplay_mic
carplay_mic aec off
carplay_mic aec on
carplay_mic delay 0
carplay_mic delay 10
carplay_mic mute
carplay_mic unmute
```

AEC on/off and delay changes are applied by the next capture read. The requested mode and delay survive session restarts in RAM, not across reboot. Delay accepts 0 through 200 ms. It is additional acoustic/reference offset, not the network round trip. Start at zero; increasing it beyond the actual echo delay can make cancellation worse. During continuous far-end speech, compare ref_ok, ref_miss, ref_drop, process_max_us, capture_gaps, peak and clipped counters. Occasional startup/idle reference misses are expected. Persistent misses, clipping or processing time exceeding a 10 ms frame need investigation before further tuning.

## Music playback

Default mixed playback keeps the shared hardware clock at 48 kHz. A requested native-rate direct route yields while microphone capture is running, preventing 44.1 kHz playback from retuning 48 kHz capture. This protection also prevents native-rate PCM probe output from retuning a running microphone.

Partially overlapping decoded packets retain their new suffix; fully stale packets remain discarded. Diagnostics expose overlap_trimmed separately from negative_ts. Media lanes enable the existing bounded mixer clock correction with a 40 ms target. The 102/media mixed route starts or recovers with approximately 40 ms buffered PCM; a finite tail waits at most 80 ms for more data. Voice and local prompts bypass this media priming condition. Existing authentication, packet rejection, codec formats and local critical-sound preemption remain intact.

```text
carplay_status
carplay_audio_path mixer
carplay_audio_path direct
carplay_audio_probe capture
carplay_audio_probe status
carplay_audio_probe replay
```

Compare actual last_output, not only the requested route. The PCM probe is a diagnostic that changes playback behavior; native 44.1 kHz replay is unavailable while a microphone owns the 48 kHz clock. Do not disconnect the receiver between capture and replay because session teardown releases the probe. Record decoded, decode_err, negative_ts, overlap_trimmed, silence_frames, late, overflow, queue_busy and device_errors before and during the same music excerpt.

## Verification and board acceptance

Host tests exercise the actual capture adapter, AEC-enabled and disabled microphone workers, real AAC decoding through mixed and direct output, partial-overlap handling, shared-clock protection and output reference delivery. A timestamped synthetic echo test checks cross-chunk alignment, attenuation and preservation of an independent near-end signal during double talk. Virtual devices and synthetic signals do not establish D213 acoustic performance or music quality.

```sh
make -C application/rt-thread/rtcarplay BUILD=build-network-validation -j8 check
```

On the board, compare AEC on/off at the same moderate speaker volume with the remote caller using a headset. Test far-end-only speech, near-end-only speech and double talk; record intelligibility, residual echo and diagnostics. Then test the same downloaded music over wireless and wired CarPlay, call/Siri interruption and return to music, and simultaneous Ethernet sharing. Retain the existing wired stutter/poor-audio baseline for comparison. Actual echo reduction, music improvement, CPU headroom and sustained full-duplex stability remain NOT_RUN until the replacement image is tested on the board. No new PR, branch or remote publication is part of this local validation.

## Local delivery

| Check | Result | Evidence |
| --- | --- | --- |
| Full application Host suite | PASS | Actual sources under ASan/UBSan, including connection, media, input, radio/bond, USB and lifecycle regressions. |
| AEC and capture additions | PASS | AEC enabled/disabled workers, DMA/backlog timestamps and rollback, cross-chunk reference, runtime argument validation, synthetic echo and independent near-end double talk. Stable synthetic residual energy ratio is approximately 0.000521; this is not an acoustic measurement. |
| Music and reference delivery | PASS | Real AAC on default mixed and direct routes, partial-overlap suffix preservation, active-microphone clock protection and queued-reference delivery. |
| Ethernet sharing | PASS | Both actual lwIP routing and application supervisor CTest executables; device boundaries modeled. |
| Actual LVGL renderer/events | PASS | Twelve 1024x600 previews and existing network/settings/device/audio/pairing interactions. |
| GUI/Headless complete images | PASS | Nine IMG components verified for bounds and CRC32; same-source cached Bootloader. Headless image remains byte-identical to the previous delivery. |
| Board/iPhone acceptance | NOT_RUN | User must test this new image; previous Ethernet/iPhone success applies to the earlier image. |

The GUI image is output/audio-repair-validation/gui/d213-carplay-gui.img with SHA256 `082ea3bd8cc9b8e0944c17f2aa09cb8cdbd694f9896da51b5d32c80e08d0464d`. Headless SHA256 is `1110fe5b33c64c323cbefb2cea665755809055458d039518b158a0bc82b5af6b`. The directory contains exact configurations, image layout, symbols, Bootloader, build/Host logs, changed-source snapshot, binary patch and checksums. The source manifest records base commit 838eef85 plus these local changes. The designated checkout retains the delivered GUI configuration.
