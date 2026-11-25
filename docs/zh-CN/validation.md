[English](../validation.md) | [简体中文](validation.md)

# 硬件与验证

| 目标 | 已实现范围 | Host | 交叉构建 | 实机 |
| --- | --- | --- | --- | --- |
| D213、RTL8733BS、128 MiB NAND | RT-Thread CarPlay、Launcher、Fusion 与 BSP 服务 | Demo Actions 回归 | D213 固件 Actions | 本次迁移 `NOT_RUN` |
| D213、AIC8800 | 计划支持，型号和模组适配待确认。 | `NOT_RUN` | `NOT_RUN` | `NOT_RUN` |
| 嵌入式 Linux | LVGL Preview 与适配层模型，生产 Receiver 为计划。 | Launcher 和 Fusion 渲染 | `NOT_RUN` | `NOT_RUN` |

[CI 指南](ci.md)列出可运行的检查。Host 或交叉构建通过时，按提交记录软件验证结果。上板测试记录源版本清单、板卡版本、无线模组、iPhone 和 iOS 版本，以及去除设备标识后的串口日志。

## D213 上板验收

- 冷启动和热启动。检查 Launcher、设置、主题和语言切换。
- 从干净的测试配对存储开始，验证有线信任和 MFi 认证、无线 BR/EDR 配对、持久化和自动连接。切换有线和无线，分别验证断开重连。
- 测试 30 FPS 和 60 FPS 视频、首帧接管、VSync 和队列背压、Fusion、全屏、本地显示恢复，以及各显示模式下的单点和多点触控。
- 验证音乐、通话、Siri 和麦克风，以及中断和返回 Launcher 后的音频恢复。
- 在预定测试台验证正在播放与封面、导航以及受支持的车辆和 GNSS 状态。
- 验证网络共享、Wi-Fi STA 与共存，以及 USB 文件同步。
- 验证恢复 IMG，再通过 A/B OTA 更新非活动 OS/Rodata 分区对，确认 `/data` 保留、重启和回滚行为。
