[English](../ota-mirror.md) | [简体中文](ota-mirror.md)

# 固件镜像服务

滚动预发布版本 `nightly` 使用 SDK 构建产生的原名发布完整 `.img`。从发布说明复制镜像直链，填入**系统设置 → 系统更新 → 固件地址**。

部署内网镜像时，将同一个 `.img` 放在普通静态文件服务器上。例如在服务器上运行 `python3 -m http.server 8080 --directory /srv/carplay`。若 `/srv/carplay` 中的文件名一致，设备中输入 `http://192.168.1.20:8080/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img`。版本变化时使用 SDK 生成的实际文件名。选中接口与服务器同网段时，HTTP 可在没有默认网关的情况下工作。

设备根据镜像内的板型、NAND 布局、必需组件及 CRC32 校验兼容性和完整性。HTTP 下载请使用可信来源。Nightly 发布中的 `SHA256SUMS` 可供手动复核。

安装流程和 A/B USB 基线见[固件更新](ota.md)。
