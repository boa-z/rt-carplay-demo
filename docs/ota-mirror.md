[English](ota-mirror.md) | [简体中文](zh-CN/ota-mirror.md)

# Firmware mirror

The rolling `nightly` prerelease publishes the SDK-built `ota.cpio`. Copy its asset link into **Settings → System update → Firmware URL**.

For a LAN mirror, put `ota.cpio` on a static file server. For example, run `python3 -m http.server 8080 --directory /srv/carplay` on the server and enter `http://192.168.1.20:8080/ota.cpio` on the device. A server on the selected interface's subnet is reachable over HTTP without a default gateway.

The device checks the package's board, partition mapping and CPIO checksums. Use a trusted source for HTTP downloads. `SHA256SUMS` in the Nightly release is available for manual verification.

See [Firmware updates](ota.md) for installation and the A/B USB baseline.
