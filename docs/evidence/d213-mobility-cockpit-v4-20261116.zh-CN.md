# D213 Mobility Cockpit V4 原生 UI — 主机证据

[English](d213-mobility-cockpit-v4-20261116.md) | [简体中文](d213-mobility-cockpit-v4-20261116.zh-CN.md)

用户要求按锁定的 V4 设计包就地重构 demo UI，保留媒体音量、屏幕亮度、中英文、主题以及 CarPlay 全屏/融合显示选择，并清理已退役的测试页面。本检查点记录源码树在主机上证明了什么，以及哪些仍需板端验证。此处任何内容都不是板端或协议验收结果。

## 源码状态

| 项目 | 值 |
| --- | --- |
| 集成分支 | `carplay-oss` |
| V4 工作前的基线版本 | `2917135e666025d315e6cc069734e9776df39024` |
| 变更/新增文件 | 49 个路径（见提交信息中的变更报告和 `git show --stat`） |
| 可移植 SDK | `packages/third-party/rt-carplay` 未改动；未加入 LVGL、HMI、Android Auto 或 CarLife 依赖 |
| 新建应用或 defconfig | 无。就地修改现有 `rtcarplay` 应用及其两个 defconfig |

## 构建

使用 `d21x_demo128-nand_rt-thread_rtcarplay_defconfig` 执行 `scons -j8` 退出码 0，生成 `output/d21x_demo128-nand_rt-thread_rtcarplay/images/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img`（本版本 5,276,160 字节）。当前配置包含 `AIC_USING_PWM3`、`AIC_PWM_BACKLIGHT`、通道 3、1 kHz、`AIC_PWM_BRIGHTNESS_LEVEL=72`，且不再有 `AIC_PANEL_ENABLE_GPIO`；`AIC_SDMC1_HOTPLUG_PIN` 仍为 `PC.6`。

## 主机测试

| 检查 | 结果 | 说明 |
| --- | --- | --- |
| `check-lubanlite-demo`（`test_lubanlite_demo`） | PASS | 场景已按音频页退役重写：打开的 manager 排除公共请求；空闲轮询不消费任务；排队的重试被消费且不重启接收器；关闭后回到空闲，下次打开成功 |
| `check-auto-connections`（headless、GUI、fusion） | PASS | 32 轮发现/失败/恢复，以及 768→1024→768 原生会话切换 |
| `check-lubanlite-video`、`check-fusion-display` | PASS | 主机视频策略与单屏模式 |
| `check-lubanlite-touch` | PASS | 所有权、中断、坐标、32 次重启 |
| `launcher_preview` | PASS | 用真实 LVGL 9.1 在 1024×600 渲染四个页面、模态框和不支持音频状态 |
| `fusion_preview` | PASS | 预置的投影侧字节保持不变；所有融合条控件满足 `x >= 768`；767/768 触摸分界正确 |
| `check-internet-share`、`check-lubanlite-{bonds,handoff,reconnect,capabilities}`、`check-wired-{identity,trace}`、`check-iap2-endpoints`、`check-{usb,wireless}-iap2-worker` | NOT RUN（环境） | 这些目标需要本次 Windows 会话不具备的 Linux 主机条件：`-fsanitize=address,undefined` 运行时库、POSIX `sys/socket.h`/`O_CLOEXEC`，或 `PATH` 中的 `cc`。本次改动未触及它们，须在 Linux CI/`make check` 环境运行。 |
| `check-udp-burst` | NOT RUN（环境） | `tests/udp_burst/run.sh` 调用 `cc` |

NOT RUN 的失败均不涉及本次修改的文件；同一批目标在基线版本上于本 shell 中同样失败。

## 截图

`tests/launcher_preview/shots-v4/` — 15 张：仪表、互联、设置（含无音频禁用状态）、OTA（未检查、有更新、确认、安装中）和配对，由测试流程按深/浅色与中/英组合生成。

`tests/launcher_preview/shots-v4-fusion/` — 8 张：256×600 融合条的 provider 数据、数据不可用、浅色和中文变体、断开以及全屏隐藏。

全部为 1024×600 PNG，由主机测试框架用实际 UI 源码和实际 LVGL 渲染。连接、OTA 与车辆数值均为模拟值。它们不是手机画面抓取、板端截图或执行效果证明。

## ACCEPTANCE.md 对照

| # | 要求 | 结果 | 依据 |
| --- | --- | --- | --- |
| 1 | 完整 UI 以 LVGL 9.1 和 D213 BSP 编译；主机 1024×600 截图 | PASS（主机） | `scons -j8` 退出码 0；`launcher_preview` 与 `fusion_preview` 渲染 1024×600 |
| 2 | 仅四个原生页面；无额外数据/信号异常/诊断页 | PASS | 只有 `PAGE_DASHBOARD/CONNECTIVITY/SETTINGS/OTA`；退役页面已删除 |
| 3 | 仪表与融合条以速度和电量为视觉焦点；无强制额外信号 | PASS | 仪表 220 px 速度、110 px 电量；融合条 115 px/58 px |
| 4 | 深浅色覆盖四个页面和融合条；立即生效、持久化、不重启会话 | PASS（主机） | `select_theme` 重着色控件树并写 `CARPLAY_PREF_THEME`；两种主题均有截图 |
| 5 | 中英文更新所有原生标签、持久化、无缺字 | PASS（主机） | `ui_language.c` 对照表；四档 CJK 由同一并集重新生成；中文 `find_caption` 断言 |
| 6 | 音量 0/25/60/100 到达真实后端并回读；无后端时禁用 | PASS（主机模型），板端 NOT_RUN | 滑块事件调用 `carplay_volume_set()`；无音频状态渲染为禁用 |
| 7 | 亮度 5/25/75/100 安全驱动；持久化 | PASS（主机模型），板端 NOT_RUN | 范围 5..100；`carplay_backlight_set()` 驱动 PWM3；`CARPLAY_BRIGHTNESS_MIN` 下限 |
| 8 | 模式存储；全屏零 LVGL 像素且触摸正确；融合 x<768 无绘制；767/768 触摸 | PASS（主机） | `fusion_preview` 的字节一致、控件边界与触摸分界断言 |
| 9 | 连接中切换模式要么重协商，要么明确提示需重连 | PASS（主机），真机 NOT_RUN | `carplay_connection_set_display_mode()` 重建会话；拒绝以状态文本呈现 |
| 10 | 互联页反映真实进度/失败；Android Auto/CarLife+ 不假装可用 | PASS | 状态串来自管理器；"后续支持"且无后端 |
| 11 | OTA 显示真实版本/可用性/生命周期/进度/失败；无后端时禁用；不得 UI 伪造成功 | PASS（主机），板端 NOT_RUN | 只使用 `carplay_ota_*`；无服务/无网络时禁用 |
| 12 | 无高重绘装饰；每页与融合的深浅色/中英文截图 | PASS（主机） | 已提交 23 张渲染图；标签仅在变化时更新 |
| 13 | 提供测试/基准和限制简报 | PASS | 本检查点加上 V4 文档的限制章节 |
| 14 | 未经授权不建分支/PR/推送或改动无关协议核心 | 已遵守 | 工作保持在 `carplay-oss` 上；`packages/third-party/rt-carplay` 未改动 |

## 硬件边界

本版本在板端仍为 NOT_RUN：烧录镜像；5/25/75/100 的 PWM3 背光扫描及可见下限；0/25/60/100 的 `sound0` Mixer 音量与实际出声；重启后深浅色与中英文的持久化；真实视频下全屏的 LVGL 零像素行为；融合模式左侧洁净与面板上 x=767/x=768 触摸分界；与真实 iPhone 的显示模式重协商；以及针对已发布 preview 版本的 OTA 检查/下载/安装/重启路径。

原理图确认 PC.7 为送入 LED 升压的 `PWM3_B`，PC.6 为 `SDC1_DET`。该 PWM 是否在所有面板变体上都到达升压使能、编解码 Mixer 是否响应该镜像上的能力查询，只能在硬件上确认。若 Mixer 不存在，音量滑块被禁用而不谎报；若 PWM 设备不存在，亮度滑块同样被禁用。
