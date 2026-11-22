[English](../ultra-development.md) | [简体中文](ultra-development.md)

# D213 上的 CarPlay Ultra 实验

D213 Demo 通过[单屏融合](single-screen-fusion.md)组合 Classic CarPlay 视频和本地 LVGL 仪表。可移植 SDK 的[实验协议基础](../../../../../packages/third-party/rt-carplay/docs/zh-CN/ultra-foundation.md)单独说明 Vehicle State、UI Sync 和独立 Type 111 传输。

## 当前范围与验收

| 领域 | 证据 |
| --- | --- |
| Classic 融合与全屏 | 历史板端观察确认全屏视频正常，融合模式使用逐像素 alpha 后视频区域恢复。每轮实机测试分别核验确切镜像、触摸归属、重连及显示切换。 |
| Vehicle State | [显式启用的车辆实验台](vehicle-protocol-demo.md)提供模拟发动机和状态计数。历史实测中，手机声明并启用了 Vehicle State，连通两条通道并发出一条已处理的请求。请求方法和手机是否接受响应有待确认。 |
| UI Sync 和 Type 111 | SDK 已有 Host 协议覆盖。D213 尚未注册 UI Sync 渲染器或独立仪表视频接收端。真实 iPhone 互操作有待验证。 |

## 硬件边界

D213 使用 MPP H.264、DE 硬件图层和 LVGL。[MPP 解码器工厂](../../../../../packages/artinchip/mpp/ve/decoder/mpp_decoder.c)没有 HEVC 路径，[GE 接口](../../../../../packages/artinchip/mpp/include/mpp_ge.h)提供二维图形操作。官方 Ultra 需要合适的 HEVC 和 OpenGL 渲染路径、资产、呈现集成和资格认证。参见 [Apple 公开架构演讲](https://developer.apple.com/videos/play/wwdc2024/10111/)。

## 复现与记录

按[固件构建和 CI 指南](ci.md)操作。仅在模拟 Vehicle State 实验中启用 `RTCARPLAY_VEHICLE_PROTOCOL_DEMO`。记录镜像 SHA-256、SDK gitlink、开发板和 iPhone/iOS 版本、传输方式及原始日志。分别检查视频几何、本机和手机输入归属、断线重连，以及协议协商和应答。

Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI)。源码署名和第三方声明见 [SDK NOTICE](../../../../../packages/third-party/rt-carplay/NOTICE)。
