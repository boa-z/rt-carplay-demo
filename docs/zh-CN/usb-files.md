[English](../usb-files.md) | [简体中文](usb-files.md)

# USB 开发文件传输

`RTCARPLAY_USB_FILE_SYNC` 在 CarPlay 应用层组合 CherryUSB 已有的 ADB 类、sync 文件服务、演示描述符和 RT-Thread DFS 适配，不启用独立 RT-Thread adbd 包或 shell 服务。电脑仍使用 Android Platform Tools 的 `adb push` 和 `adb pull`。这项文件传输接入没有修改 CherryUSB、rt-carplay 上游源码、RTOS 或 SoC 驱动。

GUI 开发配置将 USB0 设为连接电脑的 Device，USB1 保留为 iPhone Host。唯一 Host 控制器在软件中编号为 bus 0，因此物理 USB1 不再对应 Host bus 1。运行不带 bus 参数的 `usb_ehci_status` 可查看全部已初始化 Host。此配置下 USB0 不能再接 Host 存储设备；需要该功能时关闭文件传输选项并恢复 USB0 Host 配置。

直接复用的上游演示描述符使用开发 VID/PID `FFFF:FFFF`、固定演示序列号和 WinUSB 描述符。这些属于原型身份，量产产品需要合法的专用描述符。电脑驱动关联和 adb 枚举仍需实机验证。开发服务通过 DFS 访问文件，未提供 ADB 身份认证，仅应在受控开发板启用。开发板继续管理文件系统，不把已挂载卷同时作为 USB 大容量存储导出。未采用 MTP：当前仓库的模板要求另行授权的商业实现。

## 构建与验证

应用 SConscript 直接编译第三方源码。唯一兼容处理是生成 `rt_vfs_dfs.c` 副本：当前工具链将 `st_mtime` 定义为 `st_mtim.tv_sec`，与第三方 `vfs_stat` 同名字段冲突。`tools/prepare_usb_vfs.py` 保留原许可并处理这两处引用，遇到预期外源码变化时拒绝生成，不改第三方原文件、系统头文件或 SDK 构建脚本。生成文件已忽略。

```sh
make -C application/rt-thread/rtcarplay check-usb-file-sync
```

ASan/UBSan 测试向实际上游服务输入分片请求头和数据，验证 64 KiB 二进制上传下载、文件不存在、只读错误，以及上传中途关闭服务后重新打开。VFS 使用受控测试卷，因此不代表 USB Reset、DMA、实际 DFS、电脑驱动或 CarPlay 并发通过。上游 USB Reset 可能保留未完成文件句柄，直到下次 sync 打开时清理；中断上传不具备原子性，可能留下部分文件。导入配置时应先传到临时文件，校验后再应用。

## 实机步骤（尚未执行）

电脑接 USB0，iPhone 接 USB1。确认启动日志出现 `[usb-files]`，在已确认可写的 TF 挂载点 `/sdcard` 或其他可写 DFS 挂载点传输独立测试文件：

```sh
adb devices
adb push check.bin /sdcard/check.bin
adb pull /sdcard/check.bin check-return.bin
```

比较电脑两份文件的 SHA256，再验证空文件、多兆字节文件、传输中拔插 USB0 后重新上传下载，以及 CarPlay 显示和播放音乐期间传文件。清理测试文件和启用配置使用串口 shell；未链接 `adb shell`。DFS 根目录 `/` 表示文件系统命名空间，不表示限制只能访问 `/sdcard`。不要为测试传输而覆盖正在使用的配对、OTA 或应用配置。

本轮 GUI 镜像构建和 Host 检查已通过。电脑枚举、开发板实际文件读写、USB 中断恢复以及与 iPhone 同时运行均为 **NOT_RUN**。原有有线 CarPlay 零 RX/TX 超时故障仍未解决，文件服务构建成功不代表该故障修复。
