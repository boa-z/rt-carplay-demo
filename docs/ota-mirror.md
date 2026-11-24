[English](ota-mirror.md) | [简体中文](zh-CN/ota-mirror.md)

# Firmware mirror

The rolling `nightly` prerelease publishes the complete `.img` under the name produced by the SDK. Copy the asset link from its release notes into **Settings → System update → Firmware URL**.

For a LAN mirror, copy that same `.img` to an ordinary static file server. For example, run `python3 -m http.server 8080 --directory /srv/carplay` on the server, then enter `http://192.168.1.20:8080/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img` on the device if that is the file in `/srv/carplay`. Use the actual SDK filename if its version differs. A server on the selected interface's subnet is reachable over HTTP without a default gateway.

The device uses the image's board and NAND layout, required components, and embedded CRC32 values to check compatibility and integrity. Use a trusted source for HTTP downloads. `SHA256SUMS` in the Nightly release is available for manual verification.

See [Firmware updates](ota.md) for installation and the A/B USB baseline.
