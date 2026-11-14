[English](../video-handoff-fix.md) | [简体中文](video-handoff-fix.md)

# CarPlay 连接成功但启动页遮挡视频

## 故障固件与反馈

用户刷入 `d213-carplay-gui-38217874945d6a3035293cc6831257b53d67bd92`（IMG SHA256 `cea07af4ec8a17bd2e610dee45e45d77c295eca0e4aa749e4ce4d059b1ffa20a`）后能够正常开机并连接 CarPlay，但画面未正确显示。开机成功不代表视频显示验收通过。

## 已确认的源码与二进制缺陷

`ui/video_layer_policy.c` 包含 `<sys/ioctl.h>`，却没有先包含 `<aic_common.h>`。RT-Thread libc 的 ioctl 宏将方向和结构体大小编码进命令。SDK 驱动 `bsp/artinchip/drv/display/drv_fb.c` 包含 `aic_core.h`，后者加载 `aic_common.h`，仅使用类型和命令序号编码。两个头文件均保留已有宏，因此包含顺序决定实际 ABI。

| 操作 | 错误的 UI 命令 | SDK 驱动命令 |
| --- | --- | --- |
| 读取 UI alpha | `0xc0104626` | `0x4626` |
| 写入 UI alpha | `0x40104627` | `0x4627` |

修复前 UI 目标文件的 RISC-V 反汇编包含错误常量，已构建驱动的 `aicfb_ioctl_cmds` 表则包含 SDK 常量。驱动将 UI 查询判为无效命令，策略因此保持不透明启动页，遮挡下方 DE 视频层。SDK 视频帧计数可以在 UI 未隐藏时增长，因此页面能够显示已连接，却仍遮住实际视频。

## 修正与回归覆盖

在策略文件的 RT-Thread/libc 头文件之前加载 `<aic_common.h>`，与驱动及现有 MPP Sink 保持一致，移除策略文件对 `<sys/ioctl.h>` 的直接依赖。保留旧 alpha 配置的捕获及断开后的精确恢复，UI 可见性仍由宿主管理。补充 alpha 查询失败和视频交接成功日志。

Host 回归独立检查驱动使用的数值命令，假设备拒绝其他命令值。SDK 测试头文件现在模拟带保护的 SDK 命令宏，不再隐式使用 Linux ioctl 编码。该回归在修复前因 `0x4626` 断言失败；修复后实际策略、alpha 恢复及 MPP Sink 回归在 ASan/UBSan 下通过。固件 CI 在构建 GUI 和 Headless 镜像前执行这项回归。

## 仍需开发板验收

源码和二进制中的 ABI 不匹配已经确认，是否解释已连接开发板的全部症状仍需通过修正镜像和板端日志验证。本任务在 CarPlay 检出目录中未找到 `aiburnconfig.json`，尚未读取开发板串口或刷入替换镜像。新构建的视频实机验收保持 **NOT_RUN**，直至完成板端测试。

修正 GUI 镜像上应连接手机，确认真实、持续变化的 CarPlay 画面及触摸输入，再断开并确认启动页恢复。按实际使用场景重复重连及 USB/无线模式切换。控制台应出现 `[carplay-ui] video visible, UI alpha=0`，`carplay_video status` 应显示解码帧且没有持续解码错误。如果画面仍异常，应保留完整串口日志及 `carplay_status`、`carplay_video status` 输出；不要仅凭连接状态或解码帧计数判断显示正常。音频行为保持不变。
