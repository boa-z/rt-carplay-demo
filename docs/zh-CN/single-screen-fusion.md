[English](../single-screen-fusion.md) | [简体中文](single-screen-fusion.md)

# D213 单屏 CarPlay 融合

统一 D213 镜像在 1024×600 屏幕上启用 `RTCARPLAY_SINGLE_SCREEN_FUSION`。这是 Classic Main Display 视频与 LVGL 9 本地仪表的融合，不是 Apple Ultra Local UI 渲染器，也不能证明官方 Ultra 互操作能力。SDK 不引入板级或 LVGL 依赖；不启用 LVGL launcher 的构建保留原有按面板尺寸的显示策略。

## 原生全屏与融合显示设置

进入 **设置 → 显示 → CarPlay 显示**，选择 **全屏** 或 **融合**。全屏请求原生 1024x600 视频，融合请求 768x600 视频，旁边保留 256 像素仪表。有线和无线连接均采用所选尺寸。下方 Shell 命令还提供本地仪表模式。

全屏显示原生 1024x600 CarPlay 视频，本地 UI 硬件图层完全透明，已锁存视频区域内的触摸全部交给手机。会话内可通过 OEM 图标或 `carplay_projection pause` 返回启动台。

几何变化异步执行：连接管理器先关闭 USB/iAP2 借用者，再停止旧 Receiver，以新尺寸重建 Receiver、HID、视频接收器和 `/info`，随后在同一有线/无线连接所有者上等待重连。退出期间保持本地仪表，LOCAL 保留已选手机几何；重复同尺寸请求不重连，待处理请求合并。设备/音频工具打开时可暂时拒绝切换。已应用的显示模式保存到应用配置，在连接线程启动前恢复。LOCAL 在本次启动期间保留此前手机几何，重启后使用默认融合几何，同时仍保持 LOCAL 显示。本版本真实 iPhone 重连和原生尺寸输出仍为 NOT_RUN。

「设置 → 语言」即时切换本地文案，首次启动默认英文。语言、无线发现、麦克风静音和显示模式保存在 `/data/carplay-ui.bin`。带版本和校验的记录和备份用于替换时恢复。缺失或非法数据使用默认值。保存失败会打印日志并在设置页提示。如果需要在整包刷写后保留设置，将该文件纳入私有 `/data` 备份。见[字体来源与再生成](../../ui/fonts/README.zh-CN.md)。

新连接在视频已经呈现并进入 LIVE 后，启动台关闭设置/只读页面，自动进入选定显示模式。每次成功连接只执行一次，因此在已有融合会话中主动打开设置仍可正常操作。失败或重连等待不算成功。

对应命令为 `carplay_connection display fullscreen`、`carplay_connection display fusion`、`carplay_connection display local`。`carplay_connection status` 显示选定尺寸和是否等待退出，`carplay_display status` 显示实际锁存的视频/输入尺寸。旧 `carplay_display fullscreen/fusion/local` 命令保留为仅修改合成布局的诊断，不重建会话。

物理尺寸也随区域变化：`RTCARPLAY_PANEL_WIDTH_MM`、`RTCARPLAY_PANEL_HEIGHT_MM` 描述整块 LCD 有效区，应用再按手机区域缩放。默认保留此前估计的 200x120 mm，因此融合 768x600 声明 150x120 mm，原生全屏声明 200x120 mm。默认值不是实测校准；请使用不含边框的有效区宽、高来配置密度和比例。本次修正较小像素区域仍声明整屏物理宽度的问题，是否改善用户报告的压缩感仍需手机实测。

## CarPlay 帧率

**设置 → 显示 → CarPlay 帧率**默认使用 30 fps，已保存的选项继续生效。若此前保存了 60 fps，请先改为 30 fps 回归 Siri，再用 60 fps 重复测试。重启接收端后再连接，新的 `/info` `maxFPS` 才会生效。重启开发板会重新启动接收端。D213 的 1024×600 面板时序约为 60 Hz。实际视频发送频率由 iPhone 决定，视频接收端会保留扫描输出缓冲区直到显示栅栏完成。

实机对照时，对两档分别选用全屏并播放相同的持续动画至少十秒。每段开始和结束分别执行 `carplay_status` 与 `carplay_video status`，比较 `offered_max_fps`、接收帧率、`presented_fps`、队列丢弃和各阶段耗时。有线和无线分别测试。实测 `presented_fps` 持续接近 60 才能确认开发板达到目标。

## 手机分辨率与黑边

统一镜像在融合模式声明 768x600 Main Display，物理 framebuffer 和 LVGL 仍为 1024x600。此前将手机的 1024x600 画面等比例放入左侧 768x600 区域，会得到 768x450，上下各留 75 像素黑边。手机按声明的 768x600 渲染后即可完整填满左侧，无需拉伸或裁剪；实际输出分辨率和触摸仍需 iPhone 验证。全屏模式重新声明原生 1024x600。

构建配置中的尺寸变化需要新镜像及会话，运行时 Settings 现可按上述流程重建指定原生尺寸的会话。在现有 768x600 会话内仅修改合成布局到 FULLSCREEN，仍会居中并左右各留 128 像素；要建立 1024x600 会话，请用设置页或 `carplay_connection display fullscreen`。启动页仍接收物理 1024x600 触摸，再映射到已锁存的手机逻辑尺寸。

## 模式与操作

| 模式 | 显示 | 输入 |
| --- | --- | --- |
| `FULLSCREEN` | 原生 1024x600 视频，本地 UI 图层完全透明，无设置浮层。 | 按实际锁存的视频矩形映射手机坐标，不保留本地角落。 |
| `FUSION` | 左侧 768×600 区域内等比例显示视频，右侧 256×600 显示不透明本地仪表。 | 从按下到抬起保持手机或本地 UI 的触摸归属。 |
| `LOCAL` | 整屏本地仪表，断连或连接初始化失败时也可运行。已连接的视频可以在不透明 UI 后继续解码。 | 仅本地 UI。 |

首次启动默认请求 FUSION，后续恢复保存模式。首帧之前显示启动台，视频更新和 VSYNC 成功前实际仪表呈现为 LOCAL。断连或解码器重建回到 LOCAL，后续成功视频帧恢复请求的模式。仪表页“显示”与启动台设置共用同一显示页。融合条底部的“← 仪表”按钮会切到启动台仪表页并把面板交还本地页面（会话保持连接）；此时互联页的动作按钮显示“重新显示 CarPlay”，用于恢复投影。FinSH 仍可用于诊断：

```text
carplay_display status
carplay_display fullscreen
carplay_display fusion
carplay_display local
carplay_display simulate
```

状态包含请求/实际模式、请求/锁存布局代次、视频状态、几何区域、逻辑输入尺寸、显示错误和车辆数据来源，不输出配对材料。启用本演示不会声明 Type 111、HEVC、虚构显示目录或 UI Sync 能力。

## OEM 图标与会话内切换

Demo 在有线和无线连接中都会提供 **rt-carplay** OEM 图标。固件内置的 180 × 180 PNG 来自 [Demo 标志 SVG](../../assets/rt-carplay-oem.svg)。修改 SVG 后，在装有 Pillow 的环境中从应用目录运行 `python tools/generate_oem_icon.py`，重新构建并连接以协商新的 `/info`。固件构建直接使用生成的头文件。定制宿主可以在调用 `carplay_app_start()` 前设置 `carplay_app_config_t.oem_icons`、`oem_icon_count` 和 `oem_icon_label`，素材需保留到接收端停止。

点击 CarPlay 内置的 OEM 返回入口后，Demo 立即切到本地启动台，现有会话可用于恢复投屏。在仪表条点击 **仪表** 或执行 `carplay_projection pause` 可显式请求将 MainScreen 交给宿主。

在启动台打开 **车机互联 → 重新显示 CarPlay**，或执行 `carplay_projection resume`，通过现有会话发送 `requestUI`。从 OEM 图标返回后，如果手机通过新的 `modesChanged` 确认取得 MainScreen，且 Type 110 流和显示布局仍有效，Demo 会直接显示保留帧。视频流已经更换或保留帧失效时，Demo 会等待新解码帧。即使手机暂时关闭屏幕流，只要会话仍在，恢复入口依然可用。使用 `carplay_status` 查看 MainScreen 所有权，使用 `carplay_connection status` 查看会话和视频状态。如果整个会话结束，由连接管理器重新建立连接。有线测试时保持音乐播放，点击 OEM 返回入口再恢复，检查 RTSP 会话和 iAP2 链路是否持续连接。

## 硬件合成与资源归属

MPP 继续将 H.264 解码到自己的帧缓冲，DE 负责视频缩放和位置。GUI 线程在 FUSION 中将现有不透明 UI 帧缓冲裁剪为右侧区域，并使用逐像素 alpha，让 DE 整屏 UI 平面的空白区域保持透明。D213 实测全局 alpha 255 会使该区域不透明，即使解码和图层计数正常也遮住视频。LOCAL 恢复原始几何和 alpha，FULLSCREEN 使用全局 alpha 零；跨 LVGL 缓冲切换保留当前物理地址，不增加逐帧视频软件拷贝。要求现有 UI 图层为未旋转、未裁剪的 1024×600 配置，几何不匹配会请求 LOCAL 并记录错误。

只有视频图层更新和 VSYNC 都成功后才发布新的输入矩形。手机静止画面也会随模式改变重新摆放，但不计为新视频帧。交换失败时保留两个可能仍被扫描的帧；停止或重建必须先关闭视频层并确认 VSYNC，失败则保留资源供重试。UI 切换失败会尝试回滚，由 GUI 定时器重试 LOCAL。持续的显示 ioctl 故障仍需实机诊断，软件不能保证硬件故障后的仪表可见性。

触控使用已锁存的显示矩形和协商后的逻辑尺寸。CarPlay 区域独立跟踪最多两个触点 ID。本地 UI 依然只跟踪一个触点，触点不会在两个区域之间转交。黑边和布局待锁存期间的触控会被忽略。CarPlay 触点越出视频区时在最后的有效坐标释放，另一触点可以继续移动。模式、会话或几何尺寸变化时，所有已捕获触点都会释放，包括静止的手指。短暂关中断只用于有界状态更新，期间不调用 LVGL、设备 ioctl、日志或外部回调。

上板后，在融合模式投屏地图区域内做双指缩放，操作前后各执行一次 `carplay_display status`。第二根手指被路由到 CarPlay 时，`two_finger_starts` 会增加。用 `carplay_status` 检查队列溢出或 HID 发送失败，并观察投屏地图是否缩放。

## 车辆数据与测量

速度、挡位、电量默认明确显示 **SIMULATED DATA**，变化曲线不是实车证据。初始化后调用应用接口 `carplay_fusion_set_vehicle()` 复制经过校验的 `carplay_vehicle_sample_t`，`available=false` 显示未知数据；`carplay_fusion_use_simulated()` 显式恢复模拟。没有猜测 CAN 映射或真实车辆超时，提供方负责采样和有效性策略，失效时应发布不可用状态。该应用接口独立于便携 SDK 的 Vehicle State Protocol 模型。

仪表每 250 ms 更新，不依赖连接管理器是否可用。显示新锁存视频 FPS、丢弃数、队列/解码/呈现平均墙钟耗时，以及现有 CPU usage 包启用时的负载和支持时的堆占用；不可用的 CPU/堆数据显示 `n/a`。重新摆放旧帧不会增加 FPS，原视频诊断命令继续提供详细统计。这些是接收端阶段测量，不是手机到屏幕延迟；端到端延迟需要同步激励和外部观测。不会仅凭画面静止判断连接失败，本地车辆数据继续更新。

## 验证与待完成实机项

`make -C application/rt-thread/rtcarplay check` 在 ASan/UBSan 下测试实际协调层、DE UI 策略和视频后端，覆盖布局代次、坐标边界、触摸归属与取消、提供方数据复制、裁剪/alpha 回滚、帧缓冲地址保留、静态画面切换、失败屏障和重复生命周期。`tests/launcher_preview` 使用实际 LVGL 9 软件渲染器构建 `launcher_preview` 和 `fusion_preview`，后者验证控件边界、标签、按钮、提供方不可用和断连画面。预览左侧空白不代表手机视频截图。`D213 firmware` workflow 运行两种预览，并随 Host 日志上传 PPM 图片。

2026-11-15 原生 Windows GUI/Headless 构建通过，每个完整镜像均通过九个组件的边界和 CRC 检查。完整应用 Host 测试也回归 Classic 行为；音频优先级测试只对接口允许的非阻塞锁忙结果有界重试，再断言优先级结果，生产音频逻辑未修改。

刷入确切镜像并实际观察合成及输入前，L2 保持 **NOT_RUN**。刷写前重新读取并核对私有配对/身份备份，刷写后恢复并检查自动连接。记录源码版本、干净/有修改的状态、镜像 SHA256 和原始板端日志。实机还需验证三种模式、手机和本地触摸边界、断连重连、解码异常、仪表持续运行、帧缓冲切换及资源/性能。Host 成功不能代替这些项目。L3–L5 和完整官方 Ultra 验收独立记录，仍为 NOT_RUN。

历史板端测试确认 FULLSCREEN 视频正常，使用逐像素 alpha 后恢复了 FUSION 左侧视频。应在实际面板上验证当前镜像的右侧仪表、触摸、重连和异常恢复。见[实验验收条件](ultra-development.md#当前范围与验收)。
