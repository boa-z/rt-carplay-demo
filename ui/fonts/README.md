# CarPlay UI fonts

These generated subsets are named CarPlay UI and derive from [Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc), weight 400, licensed under the [SIL Open Font License 1.1](OFL.txt). Copyright 2014–2021 Adobe. The reserved font name in the upstream license is Source. The modified subsets use a different name. Keep the license with redistributions.

The two subsets contain ASCII and the non-ASCII characters in `../ui_language.c`, at 16 and 24 pixels with 4-bit antialiasing. They fall back to LVGL's Montserrat 16 for symbols. No Apple or Windows font is included. Generated C files are excluded from clang-format.

To regenerate, install fontTools 4.67.0 and lv_font_conv 1.5.3 outside this checkout, then run `python application/rt-thread/rtcarplay/tools/generate_ui_fonts.py <NotoSansSC-variable.ttf> <lv_font_conv.js>` from the SDK root. The source font is not needed to build firmware; the checked-in generated C files are sufficient.

中文：字体为 Noto Sans SC 的开源子集，默认 16/24 像素，使用 SIL OFL 1.1 授权。修改后的字体命名为 CarPlay UI，不包含 Apple 或 Windows 字体。新增译文字符后运行生成脚本，编译固件不依赖外部字体或 Node.js。
