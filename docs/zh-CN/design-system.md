[English](../design-system.md) | [简体中文](design-system.md)

# Cockpit 设计系统

本地 System Shell 以 1024×600 为设计基准，顶部状态栏高 40 px，底部导航栏高 76 px，中间为应用容器。首页显示车速、SOC 和手机摘要。手机互联提供连接操作、正在播放及路线引导。手机详情和系统更新使用二级路由。设置页通过左侧导航和右侧详情分组呈现控件。

布局参考 [AAOS 小尺寸横屏](https://developers.google.com/cars/design/automotive-os/product-experience/system-ui/small-landscape)和 [AOSP 双栏设置](https://source.android.com/docs/automotive/hmi/car_settings/dual_pane)。预览覆盖 800×480、1024×600、1280×720 和 1920×1080 的触控导航。目前在其他分辨率下，本地界面按 1024×600 画布缩放。

| Graphite Blue 色彩角色 | 深色 | 浅色 |
| --- | --- | --- |
| 背景 | `#111214` | `#F2F3F5` |
| 表面 | `#1B1C1F` | `#FFFFFF` |
| 次级表面 | `#25272B` | `#EDEEF1` |
| 主色 | `#A8C7FF` | `#204F9F` |
| 主色容器 | `#233550` | `#DDE8FC` |
| 正文 | `#F2F3F5` | `#202227` |

`ui/carplay_ui_theme.c` 的主题角色统一管理设置行、导航、滑块和状态色。[字体指南](../../ui/fonts/README.zh-CN.md)记录字形回退规则及新翻译的覆盖检查。

GUI 读取应用快照，并通过已有模块提交操作。首页和手机互联共享手机快照及封面解码结果。活动会话或传输方式变化时清理缓存。

Fusion 单独绘制本地仪表条。1024×600 下，投屏视频占用 `x < 768` 的区域。全屏投屏时隐藏本地 System Shell。Fusion 预览检查仪表绘制与主题切换对投屏区域的影响。
