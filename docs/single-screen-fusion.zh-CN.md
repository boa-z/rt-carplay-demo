# D213 单屏 CarPlay 融合

[English](single-screen-fusion.md)

Ultra GUI 开发配置在 D213 1024×600 屏幕上启用 `RTCARPLAY_SINGLE_SCREEN_FUSION`。这是 Classic Main Display 视频与 LVGL 9 本地仪表的融合，不是 Apple Ultra Local UI 渲染器，也不能证明官方 Ultra 互操作能力。SDK 不引入板级或 LVGL 依赖，普通 Classic 和 Headless 配置保留原有显示策略。

## 模式与操作

| 模式 | 显示 | 输入 |
| --- | --- | --- |
| `FULLSCREEN` | 视频等比例适配整屏，有视频后将 UI 图层全局 alpha 设为零。 | 按已成功锁存的视频矩形映射手机坐标，排除黑边。 |
| `FUSION` | 左侧 768×600 区域内等比例显示视频，右侧 256×600 显示不透明本地仪表。 | 从按下到抬起保持手机或本地 UI 的触摸归属。 |
| `LOCAL` | 整屏本地仪表，断连或连接初始化失败时也可运行。已连接的视频可以在不透明 UI 后继续解码。 | 仅本地 UI。 |

默认请求 FUSION，在视频更新和 VSYNC 成功前实际显示 LOCAL。断连或解码器重建回到 LOCAL，后续成功视频帧恢复请求的模式。仪表页提供模式和连接页面按钮，原启动页增加 Vehicle instruments 入口。FULLSCREEN 可通过 FinSH 返回：

```text
carplay_display status
carplay_display fullscreen
carplay_display fusion
carplay_display local
carplay_display simulate
```

状态包含请求/实际模式、请求/锁存布局代次、视频状态、几何区域、逻辑输入尺寸、显示错误和车辆数据来源，不输出配对材料。启用本演示不会声明 Type 111、HEVC、虚构显示目录或 UI Sync 能力。

## 硬件合成与资源归属

MPP 继续将 H.264 解码到自己的帧缓冲，DE 负责视频缩放和位置。GUI 线程在 FUSION 中将现有不透明 UI 帧缓冲裁剪为右侧区域，并使用逐像素 alpha，让 DE 整屏 UI 平面的空白区域保持透明。D213 实测全局 alpha 255 会使该区域不透明，即使解码和图层计数正常也遮住视频。LOCAL 恢复原始几何和 alpha，FULLSCREEN 使用全局 alpha 零；跨 LVGL 缓冲切换保留当前物理地址，不增加逐帧视频软件拷贝。要求现有 UI 图层为未旋转、未裁剪的 1024×600 配置，几何不匹配会请求 LOCAL 并记录错误。

只有视频图层更新和 VSYNC 都成功后才发布新的输入矩形。手机静止画面也会随模式改变重新摆放，但不计为新视频帧。交换失败时保留两个可能仍被扫描的帧；停止或重建必须先关闭视频层并确认 VSYNC，失败则保留资源供重试。UI 切换失败会尝试回滚，由 GUI 定时器重试 LOCAL。持续的显示 ioctl 故障仍需实机诊断，软件不能保证硬件故障后的仪表可见性。

触摸使用已锁存矩形和协商逻辑尺寸，拒绝黑边和待锁存布局，禁止第二指抢占；手机手势越界时在最后有效坐标抬起，模式、会话或几何变化时取消触摸。本地手势跨区域仍属于本地 UI，定时取消也能处理静止手指。短暂关中断只用于有界状态复制，不在其中调用 LVGL、ioctl、日志或外部回调。

## 车辆数据与测量

速度、挡位、电量默认明确显示 **SIMULATED DATA**，变化曲线不是实车证据。初始化后调用应用接口 `carplay_fusion_set_vehicle()` 复制经过校验的 `carplay_vehicle_sample_t`，`available=false` 显示未知数据；`carplay_fusion_use_simulated()` 显式恢复模拟。没有猜测 CAN 映射或真实车辆超时，提供方负责采样和有效性策略，失效时应发布不可用状态。该应用接口独立于便携 SDK 的 Vehicle State Protocol 模型。

仪表每 250 ms 更新，不依赖连接管理器是否可用。显示新锁存视频 FPS、丢弃数、队列/解码/呈现平均墙钟耗时，以及现有 CPU usage 包启用时的负载和支持时的堆占用；不可用的 CPU/堆数据显示 `n/a`。重新摆放旧帧不会增加 FPS，原视频诊断命令继续提供详细统计。这些是接收端阶段测量，不是手机到屏幕延迟；端到端延迟需要同步激励和外部观测。不会仅凭画面静止判断连接失败，本地车辆数据继续更新。

## 验证与待完成实机项

`make -C application/rt-thread/rtcarplay check` 在 ASan/UBSan 下测试实际协调层、DE UI 策略和视频后端，覆盖布局代次、坐标边界、触摸归属与取消、提供方数据复制、裁剪/alpha 回滚、帧缓冲地址保留、静态画面切换、失败屏障和重复生命周期。`tests/launcher_preview` 使用实际 LVGL 9 软件渲染器构建 `launcher_preview` 和 `fusion_preview`，后者验证控件边界、标签、按钮、提供方不可用和断连画面。预览左侧空白不代表手机视频截图。专用 Ultra workflow 运行两种预览，并随 Host 日志上传 PPM 图片。

2026-11-15 原生 Windows GUI/Headless 构建通过，每个完整镜像均通过九个组件的边界和 CRC 检查。完整应用 Host 测试也回归 Classic 行为；音频优先级测试只对接口允许的非阻塞锁忙结果有界重试，再断言优先级结果，生产音频逻辑未修改。

刷入确切镜像并实际观察合成及输入前，L2 保持 **NOT_RUN**。刷写前重新读取并核对私有配对/身份备份，刷写后恢复并检查自动连接。记录源码版本、干净/有修改的状态、镜像 SHA256 和原始板端日志。实机还需验证三种模式、手机和本地触摸边界、断连重连、解码异常、仪表持续运行、帧缓冲切换及资源/性能。Host 成功不能代替这些项目。L3–L5 和完整官方 Ultra 验收独立记录，仍为 NOT_RUN。

[首次实机刷写记录](evidence/d213-fusion-first-flash-20261115.md)记录初次连接失败。[后续刷写与 alpha 试验](evidence/d213-vehicle-controls-flash-20261115.md)已记录真实视频、Vehicle State 协商及左侧黑屏：用户确认 FULLSCREEN 正常，FUSION 改为逐像素 alpha 后左侧视频恢复。完整 L2 验收仍未完成，运行时修正还需通过新编译镜像验证，并检查右侧仪表、触摸、重连和异常恢复。各项当前状态见该检查点；上文历史 NOT_RUN 记录不能作为新固件验收。
