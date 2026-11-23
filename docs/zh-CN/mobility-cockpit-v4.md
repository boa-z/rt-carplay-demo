[English](../mobility-cockpit-v4.md) | [简体中文](mobility-cockpit-v4.md)

# Mobility Cockpit V4 原生 UI

V4 设计包（`Mobility_Cockpit_UI/mobility_cockpit_v4`）在 1024×600 上用四个面向产品的 LVGL 9 页面替换 Launcher V2 以调试信息为主的页面。改动限定在现有 `rtcarplay` 应用、板级 pinmux 和 defconfig 内。没有新建应用、新建 defconfig 或加入 SDK 依赖，可移植的 rt-carplay SDK 仍是纯协议库，不包含 LVGL 或 HMI 代码。

## 范围

| 页面 | 内容 |
| --- | --- |
| 仪表 | 速度为主读数，剩余电量及百分比条，连接阶段与时钟，仪表/设置入口 |
| 车机互联 | CarPlay 状态、失败文本、连接/重试/恢复操作、配对状态；Android Auto 与 CarLife+ 标注为"后续支持"，不模拟后端 |
| 系统设置 | 媒体音量、屏幕亮度、外观（浅色/深色；自动禁用）、语言、CarPlay 显示（全屏/融合）和系统更新入口 |
| 系统更新 | 当前版本与分区、真实检查/下载/安装/重启/失败状态并在已知时显示进度、安装确认、升级服务可用性 |

随旧 demo 一并删除：独立车辆数据页、车辆信号异常页、音频测试页及其 worker、诊断页、已保存设备/文件管理页、车机网络页和旧的三模式显示页。其状态机、所有权锁和测试从 `app/connection_manager.c`、`platform/audio_test_lubanlite.c` 和 `tests/unit/test_audio_diagnostics.c` 中删除，而不是藏在菜单后面。FinSH 诊断命令仍保留给开发者。

## 图标

`tools/generate_icon_assets.py` 将 [Lucide](https://lucide.dev) 字形（ISC 许可）按设计尺寸栅格化为单色 A8 蒙版，生成 `ui/icons/carplay_icons.c` / `.h`。界面用 `lv_image_set_src()` 绘制，通过 `CARPLAY_STYLE_ICON_*` 主题角色的 `image_recolor` 着色。重新生成：

```sh
python -I tools/generate_icon_assets.py <path>/lucide.ttf
```

生成文件与字体子集一样被 `.clang-format-ignore` 排除在 clang-format 之外。

## 主题与语言

`ui/carplay_ui_theme.c` 把 V4 调色板定义为角色令牌，并整体作用于对象（背景、边框、文字颜色、与圆角无关的填充不透明度），因此切换主题不需要重跑布局。`carplay_ui_set_dark()` 立即生效；四个页面、模态框和融合条都会重新着色，选择写入设置记录。"自动"存在但禁用：本板没有环境光传感器，页面如实说明而不伪造时间表。

`ui/ui_language.c` 维护英中标签对照并支持 `carplay_ui_set_chinese()` 即时切换。中文使用 Noto Sans SC 子集，按 16/18/24/30 四档字号匹配。四个 CJK 字体文件都由 `tools/generate_ui_fonts.py` 按同一符号并集生成，因此不会出现某个字号缺字显示方块的情况。英文使用 `tools/generate_display_fonts.py` 生成的 Inter 子集。

偏好通过 `app/launcher_preferences.c` 持久化到 `/data/carplay-ui.bin`。记录包含数值设置、蓝牙名称和校验值。文件通过临时文件和备份替换，保存失败时设置页会提示。

## 蓝牙名称

进入 **设置 → 蓝牙**，输入 1–28 个可打印 ASCII 字符并点击 **应用**。重启开发板后，控制器的经典蓝牙本地名称和发现 EIR 生效。默认名称为 `rt-carplay`。蓝牙地址、配对记录、Wi-Fi 网络名称和 Bonjour 名称各自沿用原有值。检查串口中的 `Bluetooth local name`，并用手机扫描确认。已有配对的手机可能继续显示缓存名称。

如果 iPhone 能列出接收端但连不上，请在点击设备名之前和之后各执行一次 `carplay_bt status`。接收端在空闲时每 10 秒检查一次 Page Scan 和 Inquiry Scan，控制器检查失败后每 2 秒重试，期间维持 RFCOMM 监听。检查串口中的 `READY radio recovered`，以及后续的 `BR incoming connection`、`incoming Classic page`、ACL、配对和 RFCOMM 事件。如果未出现 `BR incoming connection`，说明这次日志未捕获到入站 HCI 事件。分别验证保留配对记录重连和首次配对。

## 音量与亮度

两个控件都绑定真实后端；后端缺失时控件被禁用，数值位显示"不支持"，提示行显示"此设备不支持"，不会模拟数值。数值位沿用参考设计的裸百分比（卡片标题已说明是什么量），因此任何语言都不会溢出；OTA 状态行同样采用缩短的文案。

| 控件 | 后端 | 约定 |
| --- | --- | --- |
| 媒体音量 | `platform/audio_volume_lubanlite.c` → `sound0`，`AUDIO_TYPE_MIXER` / `AUDIO_MIXER_VOLUME` | 范围 0..100。设置时执行 `AUDIO_CTL_CONFIGURE` 后回读；只有回读等于请求值才算成功。驱动不支持 Mixer 时能力查询失败，滑块保持禁用。AirPlay SET_PARAMETER 音量是独立的远端控制，不会被覆盖。 |
| 屏幕亮度 | `platform/backlight_lubanlite.c` → PWM 通道 3（PC.7 / PWM3_B） | 范围 5..100。1 kHz 下 `rt_pwm_set` 后 `rt_pwm_enable`，回读按 `pulse/period` 换算百分比。最小值 5 保留可见下限并避免黑屏。 |

板级原理图将 `GPC_P7/UART3_RTS/PWM3_B` 作为 LCD_PWM 送入 LED 升压，而 `GPC_P6/SDC1_DET` 是 SD 卡检测输入。此前镜像用 PC.7 的固定 GPIO 电平点屏，且未占用 PC.6。此前统一的 `d21x_demo128-nand_rt-thread_rtcarplay_defconfig` 和历史 Ultra GUI 配置都改为选择 `AIC_USING_PWM3`、`AIC_PWM_BACKLIGHT`、通道 3 和 1 kHz，替代 `CONFIG_AIC_PANEL_ENABLE_GPIO="PC.7"`；`target/d21x/demo128-nand-carplay/pinmux.c` 只为 PWM3 占用 PC.7，PC.6 继续用于热插拔检测。配置统一后 Ultra 配置已删除，保留的 defconfig 继续携带这些设置。

## 显示模式

设置页通过 `carplay_connection_set_display_mode()` 写入显示模式，该逻辑由现有连接管理器实现：先关闭 USB/iAP2 借用方，停止旧 Receiver，再按请求的原生尺寸重建 Receiver/HID/sink。相同时尺寸的重复请求会合并，打开的设备/音频工具可以拒绝切换，模式持久化并在连接 worker 启动前恢复。未启用 `RTCARPLAY_SINGLE_SCREEN_FUSION` 的镜像显示"此设备不支持"，而不是假装开关存在。

- **全屏**请求 1024×600 原生视频。视频锁存期间 LVGL 层完全透明，不绘制任何本地页面、状态栏、返回控件或模态框。
- **融合**请求左侧 768×600 视频。右侧 256×600 条只绘制速度和剩余电量。融合条的 `lv_obj_create(NULL)` 屏幕刻意保持透明且不注册主题，否则主题背景会在每次换肤时重绘视频区域。

`ui/fusion_dashboard.c` 只在 256 像素宽条内绘制。`tests/launcher_preview/fusion_main.c` 用已知图案填满投影侧，并在每次融合条重绘后断言这些字节未变、且每个融合条控件的绝对边界满足 `x >= 768`。x=767 的触摸落在条外，x=768 落在条内。

## OTA

页面只读写现有 `carplay_ota_*` 服务（`platform/ota_lubanlite.c`）。它显示固件提供的当前版本和分区，以及服务的检查中/已最新/有更新/下载中/安装中/重启中/失败状态，并在已知时显示进度。检查与安装只入队，绝不在 LVGL 任务中等待；安装前会以目标版本确认。镜像未启用 OTA 服务或设备无网络时，控件禁用且后端状态显示不可用。不编造端点、分区或签名校验，也不用 UI 定时器伪造刷写成功。

## 手机控制

有线 USB iAP2 和无线 AirPlay iAP2 使用相同的正在播放、路线引导和 iPhone 状态订阅。两种连接都可用 `carplay_classic_diag on` 开启串口诊断。

进入 **设置 → 手机控制** 查看正在播放、路线引导和手机状态。正在播放卡片显示收到的歌曲、歌手、专辑、播放状态、进度和总时长。D213 Demo 接收有线 iAP2、无线 iAP2 隧道或 AirPlay 内联更新提供的封面，大小上限为 192 KiB。基线 JPEG 支持最高 2048×2048 像素，直接解码为最长边 96 像素的缩略图。PNG 支持最高 512×512 像素。封面不可用时显示音乐图标。导航卡片显示收到的目的地、当前道路、剩余距离、下一转弯距离，以及按手机提供的顺序显示最多三条导航指令和八个车道提示。状态卡片按实际收到的字段显示电量、充电、蜂窝网络和通话状态，同时显示板端 RTC 本地时间，区分手动模式、等待 iPhone 时间和已收到手机时间样本。板端校时方式通过 **设置 → 日期与时间** 配置。

**Implemented / Host Tested。** 集成测试和 LVGL 预览覆盖字段清除和封面回退。iAP2 的标题和播放状态更新如果未携带封面字段，会保留当前封面。新传输可在替换封面到达前清除旧封面。**Hardware Pending。** 在 D213 上分别通过 USB 和无线连接 iPhone，确认标题和播放状态更新后封面持续显示，手机发来替换封面后正常切换。启动并取消包含转向和车道引导的路线。在充电、通话时查看手机状态，核对板端时间和当前校时模式，再断开并重新连接。预期断开时清除会话数据，重连后随更新恢复，板端 RTC 继续走时。

## 投屏恢复

D213 视频队列按整个 H.264 访问单元接收或丢弃。显示引擎锁存已提交画面时，解码可继续进行。视频接收端保留当前扫描输出、一个已提交且等待显示中断的帧，以及一个等待提交的最新解码帧。更新的解码帧可以替换尚未提交的帧。旧扫描缓冲在显示栅栏确认后归还 MPP，停止或重建解码器时仍需等待关闭显示层的栅栏。溢出时丢弃排队画面，保留队列中最新的 SPS/PPS，并为从 IDR 首片开始的下一个访问单元留出空间。接收端通过加密事件通道请求 MainScreen 关键帧。完整 IDR 可刷新队列丢帧后的解码器参考链。解码错误或流配置变化仍需要重建解码器。

**Host Tested。** 普通、Launcher 和 Fusion 视频回归覆盖队列溢出、接纳 IDR、显示中断计数和栅栏失败时的缓冲保留。**Hardware Pending。** 在有线和无线 60 fps 投屏中多次触发 Siri，并用 30 fps 做对照。紧贴测试开始和结束各执行一次 `carplay_video status`。`async_vsync=1` 表示驱动支持非阻塞检查显示栅栏。比较会话累计值 `overflows` 在测试前后的增量。`overflow_to_idr_ms` 统计从队列溢出到 IDR 首片提交给 MPP 的最近和最大耗时，`idr_to_latch_ms` 统计从该次提交到对应或后续画面首次经 VSync 确认锁存的耗时。`presented_fps` 按两次状态查询之间的整个区间取平均，包含 iPhone 未发送新画面的时段。若发生丢帧，检查恢复耗时和 `resets`，并确认画面在原会话内恢复且没有反复闪烁。

异步模式只在队列接近满时将显示提交间隔限制为 20 ms。积压达到 12 个单元，或达到 10 个且最旧单元已等待至少 120 ms 时启用限速。积压低于 10 个即恢复正常节奏，无需等待最旧单元的年龄下降。`pressure_peak=1` 记录中等积压，此级别没有提交限速。最新的未提交帧替换较旧的输出，MPP 缺少空闲帧时可释放因限速而延迟提交的帧。首帧和 IDR 恢复绕过限速。当前阈值待 D213 实测。

用 `carplay_perf on` 采集约五秒的性能窗口，使用 `carplay_perf off` 停止报告。`show` 统计经 VSync 确认的帧。结合 `rx`、队列丢弃和 `rx_latch_ms` 一起评估，后者统计从完整画面消息接收到对应帧确认锁存的 P95 和最大值，样本仅包含已锁存的帧。`deferred` 统计遇到限速的未提交帧，`discarded` 统计提交前被丢弃的解码输出。`peak_window`、队列和 MPP 解码 P95/最大值、packet 和 frame 压力、显示提交和栅栏耗时，以及生产者分配、复制和互斥锁耗时最大值均属于本窗口。`peak_total` 仍是累计峰值。P95 为直方图估计。MPP 解码和显示耗时是可以重叠的墙钟时间。溢出行记录本窗口最近一次队列满事件及发生时的压力计数。

## 证据

用 LVGL 9 软件渲染器生成当前 1024×600 界面。启动器预览覆盖设置和 GNSS 编辑页。融合预览覆盖仪表条和全屏切换。

```sh
cmake -S application/rt-thread/rtcarplay/tests/launcher_preview -B build/launcher-preview -DCMAKE_BUILD_TYPE=Release
cmake --build build/launcher-preview -j8
mkdir -p build/launcher-v4-shots
build/launcher-preview/launcher_preview build/launcher-v4-shots
build/launcher-preview/fusion_preview build/launcher-v4-shots
```

Host 渲染使用模拟连接和设备数值。板端应分别验证面板背光、编解码器音量、投影模式和 OTA，并记录确切镜像和对应日志。
