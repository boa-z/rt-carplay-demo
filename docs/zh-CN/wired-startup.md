[English](../wired-startup.md) | [简体中文](wired-startup.md)

# 有线 CarPlay 启动与诊断

## 启动顺序

打开 CarKit 后，连接管理器在启动 USB 网络的同时运行有线 iAP2。配对可复用已保存的记录。应用等待有效的 IPv6 地址后发送会话邀请。启用可选的 IPHETH 会话策略时，板级适配只在当前 USB 挂载代次成功提交 RX 后启用有效链路和发送。原始 Carrier、有效链路和实际接收计数分别记录。维护中的传输配置见[原生 NCM 指南](wired-native-ncm.md)。

若 AirPlay TCP 尚未连接，应用可在 `RTCARPLAY_WIRED_REINVITE_DELAY_MS` 后补发一次启动邀请。应用默认 10000 ms，零表示关闭。SDK 默认关闭。[DiPlay 的有线重试实现](https://github.com/shihabal3amri/DiPlay/blob/615b32ce0b40cd0b99ce94ba23a512001553684f/shared/src/main/java/com/shilapi/xcertplay/orchestration/WiredStartAgain.kt)是有限次恢复的公开参考。

## 采集卡住的连接

保持手机连接，采集从枚举到首次 `wired exit` 或画面显示的串口输出。以下只读命令间隔五秒采集两次：

```text
carplay_connection status
carplay_usb net
ipheth_status
usb_ehci_status
netstat
```

- Stage 3 检查网卡注册、活动传输、待完成和已完成的 RX、原始 Carrier 和 IPv6 DAD。地址仍为临时状态或 RX 为零，说明问题发生在视频建立之前。
- Stage 4 对比邀请及 iAP2 ACK、`NS`/`NA`、入站 `SYN`、出站 `SYNACK`、AirPlay TCP/RTSP 和首帧。记录邀请 ACK 和有效链路后，TCP 仍可能尚未建立。
- Bulk OUT 返回 `-14`（`USB_ERR_TIMEOUT`）时，保存取消传输之前的首个 EHCI 快照，并与端点及 RX 计数对照。仅凭超时无法确认是手机还是控制器未继续推进。

## 验证

记录镜像 SHA-256，分别在板端验证启动、缓存配对重连、USB 拔插、触摸、音视频和无线回退。重试时保留配对记录。Host 测试覆盖状态切换和资源释放。MPI 测试使用 32 位和 64 位整数单元生成并校验配对记录。

```sh
make -C application/rt-thread/rtcarplay -j8 check
sh packages/third-party/mbedtls/tests/run-pair-mpi.sh output/pair-mpi-native
CC=riscv64-linux-gnu-gcc AR=riscv64-linux-gnu-ar RUNNER="qemu-riscv64 -L /usr/riscv64-linux-gnu" sh packages/third-party/mbedtls/tests/run-pair-mpi.sh output/pair-mpi-rv64
```
