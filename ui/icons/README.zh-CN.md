[English](README.md) | [简体中文](README.zh-CN.md)

# CarPlay 界面图标

蒙版使用 `lucide-static` 0.468.0 中的 [Lucide](https://lucide.dev) 官方字形，遵循 [ISC 许可证](https://github.com/lucide-icons/lucide/blob/main/LICENSE)。分发时保留许可声明。字形和尺寸见 `../../tools/generate_icon_assets.py` 的 `ICONS` 表。

脚本以八倍尺寸渲染字形，再降采样成单通道 **A8** LVGL 图像。深浅主题共同使用同一蒙版，通过 `CARPLAY_STYLE_ICON_*`（`image_recolor`）着色。生成的 C 文件不参与 clang-format。

将 Lucide TTF 放在仓库之外，从 `rtcarplay` 应用目录执行。

```sh
python -I tools/generate_icon_assets.py <path>/lucide.ttf
```

生成脚本只依赖 Pillow。固件直接使用仓库内的 `carplay_icons.c` 和 `.h`。
