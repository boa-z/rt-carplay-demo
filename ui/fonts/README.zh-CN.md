[English](README.md) | [简体中文](README.zh-CN.md)

# CarPlay 界面字体

生成的 CarPlay UI 字体源自字重 400 的 [Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc)，使用 [SIL Open Font License 1.1](OFL.txt)。版权为 Adobe 2014–2021。上游保留字体名称 Source，派生字体使用不同名称。分发时保留许可证。

正文样式以 Inter 为主，LVGL 按字符回退到相近尺寸的 Noto Sans SC 子集（16、18、24 或 30 像素）。四套子集共用从界面文案和翻译提取的字符集，再回退到 18 像素压缩字库和提供 LVGL 符号的 Montserrat。「正在播放」和「路线引导」的手机内容直接使用共享的 18 像素完整字库。切换界面语言时更新文案，控件保留字体样式和行高。车速及 SOC 大数字继续使用独立的数字子集。

新增界面文案后运行 `python tools/generate_ui_fonts.py --check`。CI 运行同一项覆盖检查，无需字体转换工具。更新子集时，在仓库之外安装 fontTools 4.67.0 和 lv_font_conv 1.5.3，再在 Demo 根目录运行 `python tools/generate_ui_fonts.py <NotoSansSC-variable.ttf> <lv_font_conv.js>`。加 `--full` 可重新生成共享的动态字库。字形覆盖以源字体为准，增加界面语言时需在预览中检查文字系统、字形整形和排版。固件直接使用仓库内生成的 C 文件。
