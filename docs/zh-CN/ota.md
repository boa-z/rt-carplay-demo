[English](../ota.md) | [简体中文](ota.md)

# 固件更新

在**系统设置 → 系统更新 → 固件地址**输入 D213 CarPlay `.img` 的 Nightly 发布直链或[内网镜像直链](ota-mirror.md)，也可在串口执行 `carplay_ota source <img-url>`。Wi-Fi 下载前进入 STA 维护模式，下载使用[选定出口](network-manager.md)。安装前退出维护并断开 CarPlay。

1. 点击**下载镜像**或执行 `carplay_ota download`。取消下载在下一个传输边界生效。HTTPS 需要先设置或同步 RTC 以便校验证书，HTTP 在时钟尚未设置时也可使用。
2. 校验完成后点击**安装更新**并确认显示的版本。串口下执行 `carplay_ota status` 获取确认命令 `carplay_ota install <candidate>`。
3. 保存新地址或重启会清除暂存镜像。发布资产文件名变化时，输入新的直链。迁移后设备会忽略旧清单地址。

工作线程根据 `/data` 剩余空间暂存镜像，上限 32 MiB。设备校验匠芯创镜像边界、板型、NAND 布局、必需组件和内置 CRC32。擦除前再次校验暂存镜像并确认其元数据与下载时一致。安装可离线进行，只写入并读回非活动槽的 OS 和 Rodata，成功后通过 SDK 提交试启动。CRC32 检测文件损坏，使用 HTTPS 时由 TLS 验证服务器身份。

## A/B 基线与恢复

先通过 USB 烧录带 `carplay_ab_baseline=2` 和匹配 Bootloader 的完整 IMG。OTA 更新非活动 OS、Rodata 并保留 `/data`。完整 USB 烧录包含数据文件系统，迁移前请备份设置。

提交试启动时记录上一已确认槽。Bootloader 统计尝试次数，达到上限后一起回滚 OS 和 Rodata。GUI 确认要求定时器运行、输入和连接管理器启动、两个文件系统挂载，以及当前与计划槽位一致。确认可离线执行且无需手机。试启动待确认期间禁止再次安装。环境保留超限回滚结果。挂死后看门狗复位及车辆安全状态门控仍需集成。

物理验证均为 **NOT_RUN**。记录 IMG 哈希、Bootloader 基线和槽位，覆盖取消下载、下载或非活动槽擦写或环境提交阶段断电、正常 A→B→A 和反复试启动失败。检查 OS/Rodata 同槽回滚以及 OTA 后设置与配对保留。正常更新使用应用路径，保留完整 IMG 用于恢复。
