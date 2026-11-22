[English](../code-formatting.md) | [简体中文](code-formatting.md)

# 应用代码格式

本应用与可移植 rt-carplay SDK 使用相同的 `.clang-format`。请使用 clang-format 23.1.1，参见 [SDK 格式化说明](../../../../../packages/third-party/rt-carplay/docs/zh-CN/code-formatting.md)。配置来源为 RT-Thread，使用 4 空格和另起一行的花括号，保留 include 顺序并关闭自动补花括号。生成字体、音频字节数组和上游适配生成文件在 `.clang-format-ignore` 中排除。

在 Luban-Lite 根目录运行 `python packages/third-party/rt-carplay/scripts/format-code.py --root application/rt-thread/rtcarplay`，追加 `--check` 只检查。界面行为和语言修改已在全量格式化之前独立提交，便于分别审查。
