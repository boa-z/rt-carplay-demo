[English](code-formatting.md) | [简体中文](zh-CN/code-formatting.md)

# Application code formatting

This application shares its `.clang-format` with the portable rt-carplay SDK. Use clang-format 23.1.1 and the [SDK formatting instructions](../../../../packages/third-party/rt-carplay/docs/code-formatting.md). The RT-Thread-derived style uses four spaces and separate-line braces, preserving include order and avoiding automatic brace insertion. Generated fonts, audio fixture bytes and generated upstream adapters are excluded in `.clang-format-ignore`.

From the Luban-Lite root, run `python packages/third-party/rt-carplay/scripts/format-code.py --root application/rt-thread/rtcarplay`; append `--check` for validation only. UI behavior and language changes are committed before bulk formatting so they remain independently reviewable.
