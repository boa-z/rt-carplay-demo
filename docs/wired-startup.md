[English](wired-startup.md) | [简体中文](zh-CN/wired-startup.md)

# Wired CarPlay startup and diagnostics

## Startup sequence

After opening CarKit, the connection owner starts wired iAP2 while USB networking comes up. Pairing can reuse a saved record. The application waits for a valid IPv6 address before sending the session invitation. With the optional IPHETH session policy, the board adapter authorizes effective link and transmit only for the current attachment generation after a successful RX submission. Raw Carrier, the effective link and received packet counts remain separate observations. The [native NCM guide](wired-native-ncm.md) describes the maintained transport configuration.

The application can send one additional startup invitation after `RTCARPLAY_WIRED_REINVITE_DELAY_MS` if AirPlay TCP has yet to connect. The application default is 10000 ms and zero disables it. The SDK defaults to disabled. [DiPlay's wired retry](https://github.com/shihabal3amri/DiPlay/blob/615b32ce0b40cd0b99ce94ba23a512001553684f/shared/src/main/java/com/shilapi/xcertplay/orchestration/WiredStartAgain.kt) is a public reference for bounded recovery.

## Capture a stalled connection

Keep the phone attached and capture the serial output from enumeration through the first `wired exit` or visible picture. Run these read-only commands twice, five seconds apart:

```text
carplay_connection status
carplay_usb net
ipheth_status
usb_ehci_status
netstat
```

- During stage 3, check interface registration, the active transport, pending and completed RX, raw Carrier and IPv6 DAD. A tentative address or zero RX is a pre-video transport symptom.
- During stage 4, compare invitation and iAP2 ACK, `NS`/`NA`, inbound `SYN`, outbound `SYNACK`, AirPlay TCP/RTSP and the first frame. An invitation ACK and an effective link can both exist before TCP is established.
- On Bulk OUT `-14` (`USB_ERR_TIMEOUT`), save the first EHCI snapshot taken before cancellation. Compare it with endpoint/RX counters. The timeout alone does not identify whether the phone or controller failed to make progress.

## Verification

Use the image SHA-256 and collect the on-board startup, cached reconnect, USB removal/replug, touch, audio/video and wireless fallback results separately. Keep pair records across retries. The Host suite covers state transitions and cleanup. The MPI check exercises pair-record generation and verification with 32-bit and 64-bit limbs.

```sh
make -C application/rt-thread/rtcarplay -j8 check
sh packages/third-party/mbedtls/tests/run-pair-mpi.sh output/pair-mpi-native
CC=riscv64-linux-gnu-gcc AR=riscv64-linux-gnu-ar RUNNER="qemu-riscv64 -L /usr/riscv64-linux-gnu" sh packages/third-party/mbedtls/tests/run-pair-mpi.sh output/pair-mpi-rv64
```
