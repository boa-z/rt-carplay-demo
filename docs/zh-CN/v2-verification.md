[English](../v2-verification.md) | [简体中文](v2-verification.md)

# Launcher V2 验证

## 已交付行为

已完成的 [V2.1 架构基线](refactoring-v21-verification.md)保持有效。应用现在自动启动 USB 和无线发现，仲裁唯一 Receiver/媒体 Owner，重试启动和传输失败，并在断开后恢复发现。无线运行期间 USB 枚举继续待命，共享协议和媒体资源只允许一个活动会话。既有无线配对或会话不会被 USB 抢占。重试冷却限制为 5/10/20/40/60 秒。所有权与恢复细节见[连接管理器](connection-manager.md)。

实际 1024×600 LVGL 9 [Launcher V2](launcher-v2.md)提供连接进度、蓝牙确认、已存设备管理、无线设置、麦克风静音、既有音频测试和诊断。设置仅保存在 RAM，信任和绑定记录继续持久化。视频就绪必须经过成功图层提交和 vsync，不能仅依赖解码帧数。断开后恢复 Launcher 所有权。音频协商、编解码、Jitter、Mixer/Direct、加密和 AEC 算法保持不变，独立音频交接仍适用。

## 准确验证源码和产物

| 组件 | 版本 |
| --- | --- |
| 应用代码 | `bb44b499eb1591266ec1fd6c6b8a3803317eed37` |
| 可移植 SDK | `57936326c311850e006310e94d2aa9d711ba6a1e` |
| 独立 IPHETH | `2c87b69fda0d2dbeeb9df5180dd10157791d7371` |

[应用 CI 43649107384](https://github.com/boa-z/luban-lite-carplay/actions/runs/43649107384)的三个任务全部通过。[独立 SDK CI 43648984405](https://github.com/boa-z/rt-carplay/actions/runs/43648984405)通过。后续 README/报告仅修改文档，下列镜像明确对应已经测试的代码版本。

| 镜像 | 字节数 | SHA256 |
| --- | ---: | --- |
| GUI | 4,798,976 | `b5bf06acdda91d0e39509ddfe6bcbd1670d7b0f5db90b657ef2fc2087200e958` |
| Headless | 4,004,352 | `486955094da6d5bce8fc395a99bcdac63f9d6fa0c6ddcc7ad5b3753d1d037a41` |

两组下载产物均通过准确源码和子模块身份检查、全部五项 SHA256 清单检查以及九个 IMG 组件边界/CRC32 检查。本地副本位于 `output/ci-verified-43649107384/`。每份镜像包含同源码树构建的裸机 Bootloader。这些检查证明打包完整性，不能证明烧录或物理运行成功。

## 已执行验证

| 检查 | 结果和范围 |
| --- | --- |
| 应用 Host 测试 | PASS；ASan/UBSan 下执行实际组合、媒体、配对、USB 和 Owner 源码，设备与调度边界使用模型 |
| 自动 Owner | GUI 和 Headless 均 PASS；覆盖竞争、重试、无线/USB/Receiver 失败、拔出、会话丢失、首帧超时、冷却上限和时钟回绕 |
| USB 初始化 | PASS；实际 Class 在管理器订阅前绑定的设备仍被保留和发现 |
| 视频就绪 | PASS；解码计数非零但图层/vsync 失败时不会报告视频已呈现 |
| 实际 LVGL 渲染 | PASS；七份预览、可见边界及设置、设备、音频、蓝牙、管理器启动重试的 UI 事件 |
| GUI/Headless 固件 | PASS；交叉编译、裸机 Bootloader 构建和完整 IMG 打包 |
| SDK 基线 | PASS；V2.1 报告记录 55/55 CTest 和双语文档检查 |
| 独立 IPHETH | PASS；V2.1 报告记录 3/3 外部 CherryUSB/EP0 测试和双语文档检查 |
| 物理开发板验收 | NOT_RUN |

最终本地应用与渲染日志为 `output/ci-logs/v2-final-host-check.log`、`output/ci-logs/v2-startup-retry-check.log` 和 `output/ci-logs/v2-render-check.log`。预览的连接和设备数据使用模型，渲染使用真实 LVGL 与应用 UI 代码。预览成功不能证明蓝牙、面板或音频性能。

## 仓库与文档收尾

开发和发布使用现有应用分支 `carplay-oss` 与 SDK 分支 `main`，作者/提交者为 `boa-z <boa-z@outlook.com>`，使用活动 boa-z 凭证，未创建 PR。按照所有者清理旧 PR 记录的明确授权，SDK 仓库完成备份和重建。新仓库 ID 为 1458244070，已核对所有状态 PR 数为零。新根提交与此前验证的 SDK 源码仅在 README 证据/历史说明上有差异，保留全部实现和测试。原共享工作区未被修改。

经过验证的离线历史、PR 元数据、CI 日志、恢复检查和源码映射保存在 `C:/Project/Carplay/backups/20261114-v2-final/`。旧 SDK Gitlink 和 CI 链接可能需要该备份。父仓库保留上游历史。当前应用 README、连接管理器、Launcher 和验证文档具有完整中英文对应版本，历史构建记录明确以当前文档为准。机械标题/代码/字面量/链接检查补充人工语义审阅。

## 剩余开发板检查

Windows 未枚举串口，活动工作区没有 `aiburnconfig.json`，未执行烧录或串口会话。此前反馈可以启动的旧镜像不能验证本次 V2 镜像。物理冷启动、两种连接路径、同时到达、重连、长时间断开/恢复、首帧呈现、触摸交接、蓝牙确认、已存设备重置、音频输出和麦克风均为 NOT_RUN。应使用上述准确镜像和哈希执行检查并保留原始日志。呈现就绪和显示所有权修改针对此前连接成功但无画面的问题，但尚未实机确认问题已解决。
