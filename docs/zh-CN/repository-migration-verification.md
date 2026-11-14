[English](../repository-migration-verification.md) | [简体中文](repository-migration-verification.md)

# 仓库迁移与裸机验证

## 仓库身份

集成仓库为 `boa-z/luban-lite-carplay`，默认分支 `carplay-oss`，本地检出为 `C:/Project/Carplay/luban-lite-carplay`。上游远端为 `https://github.com/boa-z/luban-lite.git`，分支 `master`，基线快照 `4060f85bd27a1eb555952921139b77cc41bdeb4e`。这是独立仓库，不是 `jiachenin/luban-lite-jc` 的 Fork；发布历史由上游快照及后续 CarPlay 集成提交组成，没有引入其他产品的开发提交。原共享 worktree 保留为本地迁移备份。

迁移提交为 `f0ceb6b6`，随后 `38217874945d6a3035293cc6831257b53d67bd92` 补充升级描述符和重启回归覆盖。作者和提交者均为 `boa-z <boa-z@outlook.com>`。已发布的子模块固定版本为：

| 组件 | 提交 |
| --- | --- |
| rt-carplay | `741056aba0d6299c5fa1a5e3c9ca1974f37e14a2` |
| cherryusb-ipheth | `2c87b69fda0d2dbeeb9df5180dd10157791d7371` |

本次迁移没有创建 PR。

## 软件验证

WSL 中固定版本的 rt-carplay 通过了 `make -j8 check` 的全部 57 个测试程序及依赖边界检查。CherryUSB 适配层的真实源码测试在 ASan/UBSan 下通过，覆盖裸机信号量、队列容量与回绕、IRQ 完成通知和超时、定时器取消与自删除、Hub 连接/断开/重连、初始化失败回滚、有序释放、类型正确的升级描述符、非空事件回调及设备重复初始化/反初始化。原有 EHCI 短包/零长/完整/错误传输链、Cache 风险、诊断快照及 EP0 共享锁计时测试也通过。

GitHub Actions [运行 43618003385](https://github.com/boa-z/luban-lite-carplay/actions/runs/43618003385) 在 SDK 提交 `38217874945d6a3035293cc6831257b53d67bd92` 上完成，GUI 与 Headless 均成功。每个任务均从本仓库编译 Bootloader，核对打包二进制与该构建一致，再编译和打包 RT-Thread 应用。下载后已独立复核全部产物 SHA256。两个 IMG 都包含九个记录，其组件边界与 CRC32 检查通过，包含 DDR 升级器、Bootloader 和 OS。完整性检查器也能拒绝 OS 数据损坏、镜像截断及缺少 OS 记录的输入。

| 镜像 | 字节数 | SHA256 |
| --- | --- | --- |
| GUI | 4,778,496 | `cea07af4ec8a17bd2e610dee45e45d77c295eca0e4aa749e4ce4d059b1ffa20a` |
| Headless | 3,983,872 | `4061051253052f9246be97e9148c7ac851e4c945fa35f295252a252119035415` |
| GUI Bootloader | 363,560 | `fccc6de0361e3dfb6cb74cd08bdddf7ac40e8d6422867aa47b81971562265269` |
| Headless Bootloader | 363,496 | `27f39ce5fb24dfea4e6f4dc5316fa9914bc69568f96c16f9581bde68e0075f82` |

## 开发板反馈

2026-11-14，用户反馈刷入 `d213-carplay-gui-38217874945d6a3035293cc6831257b53d67bd92` 产物后可以正常开机，随后明确更正最初的功能反馈：CarPlay 连接成功，但屏幕未正确显示 CarPlay 画面。因此仅烧录/开机记录为 **USER_REPORTED_PASS**，CarPlay 视频显示记录为 **USER_REPORTED_FAIL**，该产物尚未通过完整 CarPlay 功能验收。本任务没有采集串口日志、逐项测试矩阵或独立的板端哈希回读。详见 [显示回归排查](video-handoff-fix.md)。

Headless 板端运行、各项 U 盘/SD 卡升级用例、OTG 角色切换及长时间重连/压力测试仍为 **NOT_RUN / 未逐项反馈**。用户的总体反馈不等同于这些路径全部执行。后续源码提交应保留各自的构建和实机状态，不能继承 `38217874` 的板端验收结果。

## V2.1 剩余范围

仓库迁移和完整 IMG CI 不代表整个 V2.1 架构任务完成。RT-Thread 应用包装层和启动页仍需完成 [V2.1 进度报告](../refactoring-v21-progress.md) 中列出的生命周期、连接管理器抽离、平台所有权和最终 SDK API 工作。rt-carplay 全量双语文档审计仍未完成。音频协商和路由行为继续冻结，通过独立音频调试交接处理。
