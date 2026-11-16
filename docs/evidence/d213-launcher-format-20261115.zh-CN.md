# D213 启动台简化与格式化检查点

[English](d213-launcher-format-20261115.md)

按用户要求，功能版本先于全量格式化独立提交并推送。集成提交 `f5a26e78899022986e8aad33cfd12ca10c4875bb` 将显示切换统一为全屏、融合、本地仪表三个选项，使用明确的应用/取消操作，加入中英文切换并缩短启动台文字。启动默认英语，语言选择保存在内存中。全屏模式请求手机原生渲染的 1024x600 会话，右上角提供小型设置入口。融合与全屏均支持有线和无线连接，并保留 `ba46280b` 的蓝牙传输方向修复。

集成提交 `3609c39b01962ca1c0fd64a85e80029b364b8e27` 单独包含格式化变更，固定 SDK `6234c52a9531a734f0ed953097a1e8ff4eb40b80`。SDK 格式化提交为 `6b8d3fe6aecdce93ae95feb1b6deac5c2fa4602c`，后续提交补充了限定范围的 LF 检出规则。两项目采用同一份源自 RT-Thread 的配置及 clang-format 23.1.1，处理 232 个 SDK 和 118 个应用已跟踪 C/C++ 文件；忽略文件列出的第三方实现、生成资产和字节夹具不参与格式化。格式化源码的非注释 C token 比较通过。两处既有 Host 源码提取脚本改为兼容结构体声明中的空白，避免 Allman 大括号破坏提取。未新增测试用例。

本地既有 SDK、集成 Host 检查、格式检查及真实 LVGL 启动台/仪表预览均通过。[功能版本集成 CI](https://github.com/boa-z/luban-lite-carplay/actions/runs/43802539956) 的 Host、GUI、Headless 均通过。[SDK CI](https://github.com/boa-z/rt-carplay/actions/runs/43804133812) 通过。[格式化版本集成 CI](https://github.com/boa-z/luban-lite-carplay/actions/runs/43804180059) 的 Host、GUI、Headless 均通过。

`3609c39b` 的 Windows GUI 构建成功。保存镜像为 `C:/Project/Carplay/rt-carplay-artifacts/ultra-research/native-3609c39b/gui/d213-carplay-ultra-gui.img`，SHA256 为 `cb188a604b7bedd1e866a0cb0fe3855cf31f86599e5243098ae3d6347d7f730f`。验收清单对应准确的集成/SDK 提交及干净源码状态，全部六项 SHA256、九组件边界/CRC32 及配置/ELF 检查通过。构建日志为 `C:/Project/Carplay/rt-carplay-artifacts/ultra-research/native-3609c39b-gui-build.log`。

开发板由用户手动测试。本检查点没有执行串口、ADB、刷写、重启或开发板连接。此镜像的中文屏幕显示、全屏设置浮层与触摸区域、应用/取消操作、有线/无线模式切换、蓝牙稳定性、重连和本地仪表持续运行均为 **NOT_RUN**。Host 预览及固件构建不能证明上述结果，也不能证明完整官方 CarPlay Ultra 支持。L0 保持源码研究 PASS，完整 Ultra 范围的 L1 保持 PARTIAL，新镜像 L2–L5 保持 NOT_RUN。
