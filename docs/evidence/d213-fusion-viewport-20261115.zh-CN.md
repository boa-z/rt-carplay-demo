# D213 融合区域：手动测试镜像

[English and exact artifact evidence](d213-fusion-viewport-20261115.md)

Ultra GUI 配置将 Main Display 协商尺寸改为 768x600，物理面板、LVGL framebuffer 和启动页原始触摸保持 1024x600。此前 1024x600 视频缩放到左侧后为 768x450，上下各留 75 像素；手机按新尺寸渲染后可完整填满左侧。实际 iPhone 输出、视觉和触摸验收仍为 NOT_RUN。

集成版本为 `8ac6f5044df68073c19cf1d3b7d88b0a5775778a`，SDK 为 `0692be508dfd84a1adb170f6bcbb390740a30963`。原生 Windows GUI 构建及完整应用 Host 回归通过，镜像 SHA256 为 `0c32245094b143f99838c36e5cbc90d0d17e04aefb877e5561f7988c57e5dbca`。六项文件散列、九个镜像组件边界/CRC32、源码版本、干净工作区记录和配置/ELF 校验均通过。镜像保存在 `C:/Project/Carplay/rt-carplay-artifacts/ultra-research/native-8ac6f504/gui/d213-carplay-ultra-gui.img`，CI 镜像与此本地镜像须分别记录身份。[CI 43786904059](https://github.com/boa-z/luban-lite-carplay/actions/runs/43786904059) 的 Host、GUI 和 Headless 均通过；两个固件产物已下载并通过源码版本、干净工作区、六项文件散列和九个组件边界/CRC32 校验，GUI 保持逻辑宽度 768，Headless 保持 1024。CI 镜像散列见英文记录，两者均未记录为已刷写。

用户负责手动刷写和测试，并明确要求 Agent 不再连接开发板。此次没有刷机或新镜像实机验收结论。手动刷写前须导出配对文件，恢复后读回校验；本轮 ADB 未发现设备导致导出失败，失败目录不能作为有效备份。历史私有备份可能早于手机后续更新的信任记录。

刷写后重新建立 CarPlay 会话，通过 `carplay_ota status` 核对版本 `8ac6f504`，通过 `carplay_display status` 核对逻辑 768x600、视频区域 (0,0) 768x600 和错误零。实际检查上下黑边、完整内容、四角和边缘触摸、右侧仪表操作、拔线后的 LOCAL 和重连后的 FUSION。若手机实际输出其他比例，仍可能出现黑边，运行计数不能代替视觉验证。

同一 768x600 会话切到 FULLSCREEN 时保持比例，左右各留 128 像素；模式按钮不会重新协商手机分辨率。普通 Classic 和 Headless 配置保持 1024x600，动态重建不同尺寸会话尚未实现。

英文证据页同时记录上一版 `1e215116` 的原生镜像和 CI 镜像身份、原生镜像烧录后默认像素 alpha 的读回结果，以及恢复六文件后再连接产生七文件的配对状态变化。L0 保持 PASS；L1 及此前 L2-L4 的有限观察保持 PARTIAL；真实手机 L5 和本镜像消除黑边的实机验收为 NOT_RUN。本次没有启用 UI Sync、HEVC 或 Type 111，不能据此宣称官方 Ultra。
