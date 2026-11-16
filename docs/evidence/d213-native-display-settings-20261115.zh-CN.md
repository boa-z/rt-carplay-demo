# D213 原生尺寸显示设置

[English and exact artifact evidence](d213-native-display-settings-20261115.md)

注意：用户随后报告 e17d8fa2 蓝牙反复重启，该版本撤回，不应继续刷写。源码确认融合连接管理器把 `status.wireless` 传给表示“有线”的参数，导致连接模式反向；现改为 `!status.wireless`。原有管理器测试的模拟接口也重复了同一个错误，现同步纠正，不新增测试。此前绿色 Host/CI 未检出此缺陷。没有读取实机日志，因此完整运行因果及修复效果仍待用户手动验证；e17d8fa2 本次运行回归记为 FAIL，替换镜像手机验收为 NOT_RUN。下列散列和构建记录用于追溯问题版本，不是下载推荐。

本次响应真正的 1024x600 手机全屏、启动台显示设置及融合画面压缩感的问题。集成版本为 `e17d8fa2d6ba00d130643f9d80c8e7988e06f6f6`，SDK 为 `06f90ad4846865746e297bf8178c71301f8c5c4d`。原生 Windows GUI 镜像 SHA256 为 `4a7add9e171cd51779fdae82b1290599802394502af0335558127367bf427ce2`，位于 `C:/Project/Carplay/rt-carplay-artifacts/ultra-research/native-e17d8fa2/gui/d213-carplay-ultra-gui.img`。源码版本、干净工作区、六项散列、九个镜像组件和配置/ELF 检查通过。集成 CI 的 Host、GUI、Headless 及 SDK CI 均通过，具体链接和日志身份见英文页。

Connections > Settings > Display mode 提供 Full screen 1024 x 600、Video + meters 768 x 600 和 Local instruments。仪表栏全屏按钮也进入相同路径。尺寸变化由连接管理器异步关闭 USB/iAP2 借用者和旧 Receiver，再以新尺寸重建 Receiver、HID、视频接收器和 `/info`，退出期间显示本地仪表。同尺寸请求不触发几何重建，待处理请求合并，LOCAL 保留手机几何；设备/音频工具可拒绝切换。设置重启恢复默认。旧合成诊断命令语义保留，原生尺寸切换使用 `carplay_connection display fullscreen|fusion|local`。

完整应用 Host 回归通过，实际管理器测试覆盖 32 轮 USB/无线尺寸切换、先关后开、LOCAL、工具占用和取消待处理请求。实际 app 的 plist 被独立读取以核对两套像素和物理尺寸。新增 LVGL 测试覆盖连接中进入 Settings、三种请求、返回及边界；Classic 和融合预览也通过。Host 成功不能证明真实手机已接受新尺寸。

此前融合像素为 768x600，但物理尺寸仍使用整屏估计 200x120 mm；现按区域缩放，默认融合声明 150x120 mm、全屏声明 200x120 mm，并提供整屏有效区宽/高 Kconfig。默认值不是实测，压缩感是否改善仍为 NOT_RUN。SDK 另补充请求类型/响应计数及最近方法、通道、结果，并接入应用 FinSH 状态；响应可能包含逐 IID 拒绝，不能证明手机接收或车辆动作。

用户在收到上一版 8ac6f504 镜像后确认没有黑边，并提供有右侧仪表的融合照片，但报告压缩感；照片没有独立版本/散列读回，不能作为本次 e17d8fa2 的验收。用户已接管手动测试，Agent 此后没有连接开发板、串口、ADB、复位或刷写操作。手动刷写前保留配对文件并核对恢复；不要刷写 e17d8fa2，替换为修正版后测试全屏重连/1024x600/边缘触摸，然后在需要时拔线回到本地仪表和 Connections，选择融合并检查比例、触摸边界及重连。选定配置和实际锁存几何须分别查看，新镜像手机验收仍为 NOT_RUN。

Apple [公开架构](https://developer.apple.com/videos/play/wwdc2024/10111/)说明 Local UI 渲染器和专用合成器使用 OpenGL，HEVC 流支持也是必需项。当前 D213 集成没有已实现并验证的 OpenGL 后端、官方资产/呈现渲染器或 HEVC 媒体路径，GE2D/DE/LVGL 融合不能替代这些能力。完整官方 Ultra 需要合适的图形/编解码平台及相应软件、资产、访问权限和认证；协议与 Host 工作继续保持可移植。

L0 保持 PASS，全范围 L1 及此前 L2-L4 的有限证据保持 PARTIAL，真实手机 L5 和本次新镜像手机验证为 NOT_RUN。本次没有在 D213 启用 UI Sync、HEVC 或 Type 111，也不代表完整官方 Ultra 验收。
