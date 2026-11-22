[English](../ota.md) | [简体中文](ota.md)

# 有线 A/B 更新

OTA 策略属于应用层，复用现有 webclient、mbedTLS 和 Luban-Lite A/B 更新器；可移植 rt-carplay SDK 不依赖更新或 UI。GUI Defconfig 启用 OTA，不新增固件方案。

旧版非 A/B 系统需要先通过 USB 烧录匹配的完整 A/B IMG。OTA 仅更新 OS 和 rodata，保留 `/data`，不迁移 Bootloader 或分区布局。请保留匹配的完整镜像用于恢复。

Settings → FIRMWARE & UPDATES 显示运行版本及源码提交。检查更新时由工作线程读取 OS/rodata 活动槽位，首次检查前显示未知。有线网 `e0` 需要链路、IPv4 地址、网关和默认路由；还需要有效时钟及受信任的 TLS 证书链。应用要求 HTTPS，并检查最终 HTTP 会话使用 TLS；保留现有 webclient 的重定向行为。

预览清单必须包含 `version`、40 位小写完整提交 `commit`、`board=d21x_demo128-nand-carplay`、十进制 `size` 和小写 `sha256`。支持的包大小为 512 字节至 8 MiB。CI 校验归档结构、内容 CRC 和确切的 OS/rodata 映射后生成清单，在其它资产上传完后发布清单。SHA256 将下载字节绑定到清单，但不构成独立固件签名；信任依赖 HTTPS、配置的 CA 根证书和发布者。

不同提交均作为候选提供，可能包含降级。确认对话框绑定打开时看到的完整提交，重新检查或候选变化会使旧确认失效。控制台 `carplay_ota status`、`carplay_ota check`、`carplay_ota install` 与 GUI 共用任务门控；`install` 表示明确同意安装上次检查显示的候选。应用命令不再提供任意 URL 安装入口。

安装前断开 CarPlay。单一工作线程在调度前占用任务，下载到 `/data/.carplay-update.cpio`，检查完整大小并对实际存储字节执行哈希，再初始化 Flash 写入。`/data` 需要足够容纳完整包的可用空间。下载或写入期间新建 CarPlay 会话会在下一次检查时中止更新；更新器未占用连接管理器。写入非活动槽位时显示进度，随后由现有 A/B 库提交槽位选择并重启。写入及重启期间保持供电。

网络、下载、存储及哈希失败会删除暂存包且不初始化 Flash。写入失败不会请求切槽或重启。环境提交失败单独提示，因为 SDK 可能已经部分修改环境，重试前应检查启动状态。由于 SDK 没有返回其落盘错误，应用在重启前会重新打开存储环境，要求 OS、rodata 下一槽位及升级标记与计划切换一致。底层 SDK 落盘和回滚实现没有修改。旧 SDK 的 `test_ota` 命令绕过应用检查，禁止与应用更新并行执行；正常更新使用 Settings 或 `carplay_ota`。

在应用目录执行 `make check-ota`，在仓库根目录执行 `python3 .ci/test-ota-manifest.py`。Host 测试运行真实工作线程实现及 SHA256，模拟 HTTP、Flash 和 RTOS 操作；LVGL 预览覆盖确认、取消、目标变化、写入和失败状态。固件构建与归档校验仅提供软件证据；实机 TLS 下载、A→B→A 启动、设置保留、断电与回滚仍需要开发板验证，Host 测试不能证明这些行为。
