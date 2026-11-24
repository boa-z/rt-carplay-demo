[English](../ota-mirror.md) | [简体中文](ota-mirror.md)

# 固件镜像服务

滚动预发布版本 `nightly` 发布 SDK 构建产生的 `ota.cpio`。复制它的发布直链并填入**系统设置 → 系统更新 → 固件地址**。

部署内网镜像时，将 `ota.cpio` 放在静态文件服务器上。例如在服务器上运行 `python3 -m http.server 8080 --directory /srv/carplay`，然后在设备中输入 `http://192.168.1.20:8080/ota.cpio`。选中接口与服务器同网段时，HTTP 可在没有默认网关的情况下工作。

设备检查更新包的板型、分区映射和 CPIO 校验和。HTTP 下载请使用可信来源。Nightly 发布中的 `SHA256SUMS` 可供手动复核。

安装流程和 A/B USB 基线见[固件更新](ota.md)。
