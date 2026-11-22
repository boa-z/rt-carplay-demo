[English](README.md) | [简体中文](README.zh-CN.md)

# CarPlay UI icons

The masks use official [Lucide](https://lucide.dev) glyphs from the `lucide-static` 0.468.0 font. Lucide is licensed under the [ISC License](https://github.com/lucide-icons/lucide/blob/main/LICENSE). Keep the notice with redistributions. The table of glyphs and sizes is `ICONS` in `../../tools/generate_icon_assets.py`.

Each icon is rasterised from the Lucide regular font at 8x and downsampled to a single-channel **A8** LVGL image. The `CARPLAY_STYLE_ICON_*` theme roles (`image_recolor`) recolour the same mask in both themes. The generated C files are excluded from clang-format.

To regenerate, run from the `rtcarplay` application directory with the Lucide TTF outside this checkout:

```sh
python -I tools/generate_icon_assets.py <path>/lucide.ttf
```

Only Pillow is needed; neither the source font nor any Node.js tool is required to build the firmware, because the generated `carplay_icons.c` / `.h` are checked in.
