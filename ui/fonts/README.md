[English](README.md) | [简体中文](README.zh-CN.md)

# CarPlay UI fonts

These generated subsets are named CarPlay UI and derive from [Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc), weight 400, licensed under the [SIL Open Font License 1.1](OFL.txt). Copyright 2014–2021 Adobe. The reserved font name in the upstream license is Source. The modified subsets use a different name. Keep the license with redistributions.

Text styles use Inter with an LVGL glyph fallback to the closest 16, 18, 24 or 30 pixel Noto Sans SC subset. The subsets share the same character set extracted from UI literals and translations. They fall back to the compressed 18-pixel face from the printable Unicode cmap of Noto Sans SC, then to Montserrat for LVGL symbols. Now Playing and route guidance use the shared 18-pixel face directly for phone-supplied text. Translation changes update label contents while the style retains its font and line metrics. The large instrument fonts retain their numeral subsets.

After adding UI text, run `python tools/generate_ui_fonts.py --check`. CI runs the same coverage check without conversion tools. To update the generated subsets, install fontTools 4.67.0 and lv_font_conv 1.5.3 outside this checkout and run `python tools/generate_ui_fonts.py <NotoSansSC-variable.ttf> <lv_font_conv.js>` from the Demo root. Add `--full` to regenerate the shared dynamic face. Coverage is limited to the selected source fonts. Adding a locale requires checking its script, shaping and layout in the preview. Firmware builds use the checked-in C files.
