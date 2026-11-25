[English](README.md) | [简体中文](README.zh-CN.md)

# CarPlay 界面字体

生成的 CarPlay UI 字体源自字重 400 的 [Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc)，使用 [SIL Open Font License 1.1](OFL.txt)。版权为 Adobe 2014–2021。上游保留字体名称 Source，派生字体使用不同名称。分发时保留许可证。

固定界面文案使用 16、18、24、30 像素子集，包含 ASCII 和翻译标签使用的字符。`carplay_cjk_full_18.c` 是 18 像素压缩字库，覆盖源字体的全部可打印 Unicode 映射，包括收录的补充平面字符。「正在播放」和「路线引导」在中英文界面共用该字库。生成的 C 文件不参与 clang-format。

重新生成时，在仓库之外安装 fontTools 4.67.0 和 lv_font_conv 1.5.3，然后在 Demo 根目录运行 `python tools/generate_ui_fonts.py <NotoSansSC-variable.ttf> <lv_font_conv.js>`。加 `--full` 只重新生成动态文本使用的完整字库。固件直接使用仓库内已有的 C 文件。
