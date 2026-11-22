# CarPlay UI fonts

These generated subsets are named CarPlay UI and derive from [Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc), weight 400, licensed under the [SIL Open Font License 1.1](OFL.txt). Copyright 2014–2021 Adobe. The reserved font name in the upstream license is Source. The modified subsets use a different name. Keep the license with redistributions.

The four static UI subsets contain ASCII and the non-ASCII characters used by translated labels, at 16, 18, 24 and 30 pixels. `carplay_cjk_full_18.c` is a separate, compressed 18-pixel face built from the complete printable Unicode cmap of the source Noto Sans SC font. It is shared by Now Playing and Route guidance in both UI languages. Coverage is that of Noto Sans SC, including its supplementary-plane glyphs. Generated C files are excluded from clang-format.

To regenerate, install fontTools 4.67.0 and lv_font_conv 1.5.3 outside this checkout, then run `python application/rt-thread/rtcarplay/tools/generate_ui_fonts.py <NotoSansSC-variable.ttf> <lv_font_conv.js>` from the SDK root. Add `--full` to regenerate only the shared dynamic face. The checked-in C files are sufficient for a firmware build.

中文：固定界面文案使用 16、18、24、30 像素的 Noto Sans SC 子集。「正在播放」和「路线引导」在中英文界面共用一套 18 像素压缩字库，覆盖 Noto Sans SC 源字体映射的全部可打印字符，包括源字体收录的补充平面汉字。用 `--full` 可独立再生成完整字库。字体使用 SIL OFL 1.1 授权，编译固件直接使用仓内生成的 C 文件。
