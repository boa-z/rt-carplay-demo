# CarPlay UI icons

The masks use official [Lucide](https://lucide.dev) glyphs from the `lucide-static` 0.468.0 font. Lucide is licensed under the [ISC License](https://github.com/lucide-icons/lucide/blob/main/LICENSE). Keep the notice with redistributions. The table of glyphs and sizes is `ICONS` in `../../tools/generate_icon_assets.py`.

Each icon is rasterised from the Lucide regular font at 8x and box-filtered down to a single-channel **A8** LVGL image, so one bitmap serves every colour: the page code recolours it at runtime through the `CARPLAY_STYLE_ICON_*` theme roles (`image_recolor`), in both themes. The C files are generated and excluded from clang-format.

To regenerate, run from the `rtcarplay` application directory with the Lucide TTF outside this checkout:

```sh
python -I tools/generate_icon_assets.py <path>/lucide.ttf
```

Only Pillow is needed; neither the source font nor any Node.js tool is required to build the firmware, because the generated `carplay_icons.c` / `.h` are checked in.

中文：图标为 Lucide 官方字形（ISC 授权），由 `tools/generate_icon_assets.py` 生成 A8 蒙版，运行时按主题着色。修改图标清单后需重新运行生成脚本；编译固件不依赖外部字体。
