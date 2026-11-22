[English](ultra-development.md) | [简体中文](zh-CN/ultra-development.md)

# CarPlay Ultra experiments on D213

The D213 Demo combines Classic CarPlay video and local LVGL instruments through [single-screen fusion](single-screen-fusion.md). The portable SDK documents its separate [experimental protocol foundations](../../../../packages/third-party/rt-carplay/docs/ultra-foundation.md), including Vehicle State, UI Sync and independent Type 111 transport.

## Current scope and acceptance

| Area | Evidence |
| --- | --- |
| Classic fusion and fullscreen | Prior board observations confirmed fullscreen video and that pixel alpha restored the video region in fusion. Verify the exact image, touch ownership, reconnection and display transitions for each new board test. |
| Vehicle State | The [opt-in vehicle bench](vehicle-protocol-demo.md) provides a simulated engine and status counters. A prior phone session offered and enabled Vehicle State, connected both lanes and issued a processed request. The request method and phone acceptance of the response remain unverified. |
| UI Sync and Type 111 | The SDK has Host protocol coverage. D213 has no registered UI Sync renderer or independent cluster sink. Real iPhone interoperability remains unverified. |

## Hardware boundaries

The D213 path uses MPP H.264, DE hardware layers and LVGL. The [MPP decoder factory](../../../../packages/artinchip/mpp/ve/decoder/mpp_decoder.c) lacks an HEVC path, and the [GE interface](../../../../packages/artinchip/mpp/include/mpp_ge.h) exposes 2D graphics operations. Official Ultra requires an appropriate HEVC and OpenGL rendering path, assets, presentation integration and qualification. See [Apple's public architecture presentation](https://developer.apple.com/videos/play/wwdc2024/10111/).

## Reproduce and report

Use the standard [firmware build and CI guide](ci.md). Enable `RTCARPLAY_VEHICLE_PROTOCOL_DEMO` only for the simulated Vehicle State bench. Record the image SHA-256, SDK gitlink, board and iPhone/iOS versions, transport and raw logs. Check video geometry, local and phone input ownership and disconnect/reconnect separately from protocol negotiation and received responses.

Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI). Source attribution and third-party notices are maintained in the [SDK NOTICE](../../../../packages/third-party/rt-carplay/NOTICE).
