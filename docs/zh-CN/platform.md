[English](../platform.md) | [简体中文](platform.md)

# 架构与平台适配

## 所有权与数据流

两个平台共用 `app/receiver_session.c` 创建和销毁 Receiver 并注册回调，使用 `app/connection_retry.c` 计算有界重试延迟，使用 `app/phone_track.c` 合并 Now Playing 更新。各平台的 Receiver 所有者决定何时启动和结束会话，并负责按顺序释放借用的媒体资源。公共 Phone Hub 根据复制后的曲目信息显示播放状态和进度。

RT-Thread 由 `app/connection_manager.c` 仲裁传输方式。`app/carplay_app.c` 的轮询线程唯一拥有 Receiver，发布会话快照，并消费命令队列。Launcher 和 Fusion 渲染快照并提交用户请求。停止和切换流程见[会话快照](session-snapshots.md)和[连接管理](connection-manager.md)。

平台输出端在异步解码、显示和 VSync 期间保留媒体缓冲区所有权。适配时延续有界队列、背压和释放路径。复用 SDK 的平台描述结构，让音视频缓冲区继续沿现有路径流转。

## 依赖审计

| 范围 | 当前依赖 | 移植边界 |
| --- | --- | --- |
| Receiver 与命令 | 公共 `rtcp/receiver.h`、平台和媒体描述以及会话快照。应用组合和 iAP2 使用内部 `carplay_ctrl.h`、`iap2_session.h`、`iap2_stream.h` 及 AirPlay plist、发现辅助函数。 | 公共 Receiver API 提供等价契约后逐步迁移调用点。 |
| 有线与身份 | CherryUSB、USBMUX/lockdown 与 Host 钩子、原生 NCM 和 lwIP、I²C MFi、RT-Thread DFS、mbedTLS 及 AIC CE TRNG。Demo 使用内部 `lockdown.h` 和 `usbmux_host.h`。 | USB Ethernet 驱动留在 cherryusb-ipheth。USB、配对持久化、认证和随机数通过平台适配接入。 |
| 媒体与 UI | RT-Thread 设备、MPP/DE 与 framebuffer 图层命令、触摸、LVGL，以及内部混音、焦点、语音和抖动缓冲头文件。Host 测试也直接编译部分 SDK 内部源文件。 | 保留解码帧生命周期、异步 VSync 和音频回收。Host 白盒测试随 SDK 提交固定。 |
| 系统服务 | RT-Thread 线程与 DFS、音频/PWM/RTC、lwIP/AP 服务、CherryUSB ADB，以及 Luban-Lite A/B OTA 与 NAND 分区映射。 | 按服务接入现有应用与 SDK 的平台接口。Bootloader、分区布局和打包由 BSP 负责。 |

## 无线硬件变体

Realtek 在 `platform/rtcp_wifi_lubanlite.c` 使用 `wifi_conf.h` 和 `wifi_util.h`，蓝牙使用 Zephyr H:5、Classic BR/EDR、SDP、RFCOMM 和配对持久化。AIC8800D40 使用 SDK 的 SDIO WLAN 和蓝牙固件初始化，蓝牙传输采用 Zephyr H:4。RT-Thread SoftAP 启用前通过 SDK 设置 Apple Vendor IE。

两个目标的 OTA 固件都在**设置 → 连接 → Wi-Fi**提供 STA 维护模式，并显示 CarPlay 热点的实际状态。进入维护模式时暂停 CarPlay 连接管理，停止热点后启用 STA 扫描和连接。退出维护模式时断开 STA 并恢复原 CarPlay 连接策略。应用以互斥方式使用 AP 和 STA。AIC8800D40 上板需验证 Beacon 和 Probe Response 的 Vendor IE、蓝牙配对及配对恢复、STA 扫描及 WPA2 关联、DHCP 和 OTA 可达性，以及退出维护后无线 CarPlay 恢复。

AIC8800D40 是支持 A/B OTA 的独立硬件目标。其 CPIO 使用专用兼容 ID，RTL8733BS 保留旧 ID，两种目标均会在写 NAND 前拒绝另一目标的 CPIO。

## Linux 移植

Linux 主循环拥有 Receiver，并在同一线程同步处理 Launcher 命令。Launcher 读取包含 Receiver 和视频流代数的会话快照。切换端点时清理本地触点归属，避免把旧触点发送到新会话。libusb 和 ALSA 继续使用独立工作线程。Linux 在 USB 端点地址就绪时优先选择 USB，RT-Thread 通过连接管理线程预留传输方式。

本机可使用 `cmake -S . -B build/linux -DRTCP_ROOT=/path/to/rt-carplay -DRTCP_MBEDTLS_DIR=/path/to/mbedtls` 和 `cmake --build build/linux` 构建 Linux LAN Receiver。`CARPLAY_USB`、`CARPLAY_BLUEZ`、`CARPLAY_ALSA`、`CARPLAY_LVGL` 和 `CARPLAY_AIC_MPP` 独立启用。使用 LVGL 时需传入 `-DLVGL_ROOT=/path/to/lvgl`，版本见 `.ci/dependencies.env`。Luban 的 D21x Linux 固件启用全部五项。Linux 的 `theme`、`display-mode` 和 RT-Thread 的 `CPUI` 继续使用现有文件格式。

两个生产 Launcher 入口仍负责组装各自页面与绑定平台服务。外壳、Dashboard、Phone Hub 和 Fusion 仪表使用公共控件。RT-Thread Preview 使用其生产 Launcher 和这些公共控件。

## Classic 配件认证

应用为 USB iAP2、蓝牙 RFCOMM iAP2、Wi-Fi 隧道和 AirPlay MFiSAP 选择同一个 `rtcp_mfi_provider_t`。Linux 在 USB 工作线程、Receiver 和无线服务停止后释放它。RT-Thread 在 Receiver 重启时保留所选 Provider，并串行访问认证芯片。芯片探测失败后，硬件 Provider 会在下一次认证请求时重试。

| 后端 | Linux | RT-Thread |
| --- | --- | --- |
| MFi 2.0C / 3.0 芯片 | USB 和无线的默认后端，通过 `--i2c DEVICE` 选择 I²C 总线。 | 启用 `RTCARPLAY_MFI`，通过 `RTCARPLAY_MFI_I2C_BUS` 和 `RTCARPLAY_MFI_I2C_ADDR` 设置板级参数。 |
| 授权软件身份 | 通过 `--mfi-mode software` 指定证书、私钥和协议。`--mfi-mode auto` 使用明确配置的身份，未配置身份时选择硬件。 | 启用 `LPKG_RT_CARPLAY_MFI_SOFTWARE` 和 `RTCARPLAY_DEMO_MFI_SOFTWARE`，并实现产品身份加载接口。 |
| 外部设备或服务 | SDK 集成方可以通过 `rtcp_mfi_provider_t` 接入实例。 | 启用 `RTCARPLAY_DEMO_MFI_EXTERNAL` 并实现产品接口。 |
| 合成测试身份 | 原生 CTest 在本地测试中生成并删除临时身份。 | SDK Host 测试可脱离 BSP 验证 Provider 契约。 |

MFi 配件证书和签名身份、AirPlay Ed25519 Receiver 身份、AirPlay Pair-Setup 记录、USB Lockdown 信任记录以及 BlueZ 或 Zephyr 配对记录各自管理。重启或兼容升级后保留已有配对文件和蓝牙配对记录。Linux 启动日志会报告后端和协议版本。`MFi ... provider ready` 表示本地已初始化证书和私钥，iPhone 是否接受由真机验证确认。

### Linux 接入

在 SDK CMake 构建中启用 `RTCP_MFI_SOFTWARE`。仅部署软件后端时可设置 `CARPLAY_MFI_HARDWARE=OFF`。提供获授权的 Classic 配件证书，格式为只含证书的 DER PKCS#7 或对端接受的 DER X.509 叶证书。叶证书必须和未加密的 DER 私钥匹配。按配件身份选择 RSA/SHA-1 `2` 或 P-256/SHA-256 `3`。DiPlay 本地 Classic 配置使用协议 `3`、P-256 PKCS#8 DER `identity.pk8` 和 DER PKCS#7 `certificate.p7b`。Linux 私钥必须为服务账号拥有的普通文件，组和其他用户均无访问权限，可设置为 `0600` 或 `0400`。三项软件参数须一并提供。

```sh
build/linux/rt-carplay-demo --headless --lan eth0 \
  --state-dir /var/lib/carplay \
  --mfi-mode software --mfi-protocol 3 \
  --mfi-cert /var/lib/carplay-mfi/certificate.p7b \
  --mfi-key /var/lib/carplay-mfi/identity.pk8
```

构建启用 libusb 后可使用 `--wired`。同时启用无线时，构建设置 `CARPLAY_BLUEZ=ON`，运行时传入 `--wired --wireless wlan0 --ap-config FILE` 并准备板上的 AP、DHCP 和 BlueZ 服务。两种传输使用同一组 MFi 参数。直接使用 `--lan IFACE` 保留原有 LAN 开发流程，对端请求 MFiSAP 时，`/auth-setup` 返回 503。显式指定的凭据缺失或无效时，CarPlay 保持离线，图形版可继续显示本地 Launcher 并重试。

### RT-Thread 接入

使用固定版本的 Luban-Lite BSP 和匹配的 SDK 构建 D213 硬件版。使用独立集成目录和同一 D213 defconfig 构建软件版，通过 Kconfig 启用前述两个软件选项，Demo 会移除认证芯片后端。产品实现 `platform/carplay_mfi_provider.h` 声明的 `carplay_product_mfi_load()` 和 `carplay_product_mfi_release()`，从受保护的预置存储中将证书和私钥读入内存。D213 参考板的 `/data` 使用无法按用户限制访问的 FatFS，开发用 USB 文件服务也可访问该挂载点。产品提供受保护的存储前，参考加载接口返回 `RTCP_ENOENT`。SDK 解析完成后清零并释放临时私钥缓冲区。签名使用板级 TRNG。接入外部签名设备时，改选 `RTCARPLAY_DEMO_MFI_EXTERNAL` 并实现 `carplay_product_mfi_external()`，提供 iAP2 和 SAP 所需的 SDK 回调。缺少产品接口时拒绝启动 CarPlay，本地 HMI 和设置继续可用。

### 验证

在原生 Linux CMake 构建中启用 `-DCARPLAY_BUILD_TESTS=ON -DRTCP_MFI_SOFTWARE=ON`，运行 `ctest --test-dir build/linux --output-on-failure`。测试使用 Python 3 和 OpenSSL 命令行，覆盖本地 RTSP、软件与自动选择、缺少芯片、配置不完整、私钥权限和公私钥不匹配。合成证书由测试临时生成。使用匹配的 SDK 执行 `make check` 可验证 RT-Thread USB、无线和 Receiver 工作线程回归。SDK 版本发布后同步更新 `.ci/dependencies.env` 中的 SDK 与 Demo 测试基线。

使用 D213 或有兼容 USB、蓝牙硬件的 Linux PC，配合 iPhone 和当前后端对应的授权身份。先验证有线首次连接和信任授权，再拔插并重启，检查配对记录复用。完成无线首次配对后重复重启验证，再在无线投屏期间插入 USB，确认传输策略。在各条可用链路上检查 H.264 画面、音乐、Siri 和麦克风、通话、触摸、正在播放、导航和车辆状态。记录板型、固件与 SDK 版本、认证模式、iOS 版本和失败阶段，日志与 CI 产物中避免包含私钥和设备凭据。Host 测试和目标编译之后，仍需上板验证 iPhone 互操作和性能。
