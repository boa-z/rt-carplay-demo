# rt-carplay 模块架构与依赖关系

> 状态：架构设计说明。下文目录树描述设计目标，不保证与当前仓库逐项一致；
> 实际目录、依赖和构建步骤以 [D213 开源 CarPlay 开发与构建](build.md)
> 以及 rt-carplay 当前源码为准。应用概览与配置摘要见 [README](../README.md)。
> 目标：把 CarPlay 接收器做成一个独立、可移植、可测试的组件，而不是 Luban-Lite 的一个应用。
> 本文只描述架构和接口边界；协议实现细节见 rt-carplay 子模块文档，
> SDK 侧的构建、配置和验证状态见 [build.md](build.md)。

## 1. 目标与非目标

### 1.1 目标

1. **独立仓库**：`rt-carplay` 不依赖 ArtInChip 专有头文件、Luban-Lite 目录结构或 SCons。
2. **协议核心可在 Linux 主机上构建与单测**：无 RTOS、无板级外设也能跑协议状态机和回放测试。
3. **RT-Thread 优先**：第一目标平台是 RT-Thread；Luban-Lite 只是当前的板级适配环境。
4. **板级可替换**：同一份 core 能接不同 SoC / Wi-Fi / BT / MFi / 显示 / 音频实现。
5. **C 优先**：核心用 C99；预留 Rust 模块的 C ABI 位置，但 Rust 不在关键路径上。
6. **避免过度抽象**：只抽象真正跨平台的东西（OS、网络、BT、MFi、Wi-Fi、媒体/输入），不为了"通用"给每个外设造一层 HAL。

### 1.2 非目标（本阶段）

- 不做产品级 UI、OTA、多机型适配。
- 不实现 Apple 私有认证身份的软件模拟；MFi 始终走板载 2.0C 或合规的认证芯片。
- 不把 Realtek 驱动 / 固件、ZALink/ZABlue 二进制纳入公开仓库。
- 不承诺"完整 CarPlay"；先打通 发现 → iAP2 → AirPlay 控制 → 单路视频 + 触摸，音频随后。

## 2. 分层与依赖规则

```
┌──────────────────────────────────────────────┐
│ examples/  (d213_app, linux_sim)             │  只做 wiring，不含协议
├──────────────────────────────────────────────┤
│ port/      (rtthread, linux, future SoCs)    │  实现 include/rtcp/*.h 的接口
├──────────────────────────────────────────────┤
│ core/      (iap2, airplay, mfi, media, ...)  │  纯 C，只依赖 include/rtcp/*.h + libc
├──────────────────────────────────────────────┤
│ include/rtcp/  (公共接口，唯一契约)           │
└──────────────────────────────────────────────┘
```

**硬性规则**：

1. `core/` 不得出现 `rt_*`、`aicos_*`、`aic_*`、`esp_*`、`luban`、`artinchip`、FreeRTOS API。
2. `core/` 不直接 `#include` lwIP / mbedTLS 之外的具体平台头；密码学通过 `core/crypto/` 的薄封装。
3. 依赖方向只能向下：`examples → port → core → include`；禁止反向和环。
4. 平台差异只能出现在 `port/`，不能以 `#ifdef RT_THREAD` 的形式散落在 core。
5. 每个 core 模块必须能在 Linux 上用假接口（fake transport）单测。

CI/本地检查可用一行 grep 强制：

```sh
! grep -rnE '\b(rt_|aicos_|aic_|esp_|vTask|xTask|FreeRTOS)\w*' core include
```

## 3. 目录结构

```text
rt-carplay/
├── CMakeLists.txt              # host build + tests
├── Kconfig                     # 供 RT-Thread 集成（可选）
├── include/rtcp/
│   ├── rtcp.h                  # 版本、初始化、配置结构
│   ├── os.h                    # 时间/线程/锁/内存/日志
│   ├── net.h                   # TCP/UDP/poll 抽象
│   ├── bt.h                    # BR/EDR + SDP + RFCOMM 抽象
│   ├── mfi.h                   # Apple 2.0C 抽象（sign/cert/id）
│   ├── wifi.h                  # SoftAP / vendor IE / STA 信息
│   ├── media.h                 # 视频/音频/麦克风 sink
│   ├── input.h                 # 触摸/按键事件
│   └── log.h
├── core/
│   ├── common/                 # ringbuf, byteorder, CRC, TLV, assert, pool
│   ├── iap2/
│   │   ├── link.c              # iAP2 Link：0xFF5A、长度、序号、ACK、重传
│   │   ├── message.c           # 参数/TLV 编解码
│   │   ├── session.c           # 控制会话状态机
│   │   ├── auth.c              # RequestAuthenticationSignature 等，调用 mfi
│   │   ├── identify.c          # Identify / AccessoryInfo
│   │   └── hid.c               # 触摸/按键上报
│   ├── airplay/
│   │   ├── rtsp.c              # RTSP 请求/响应与路由
│   │   ├── plist.c             # 二进制/文本 plist 子集
│   │   ├── pairing.c           # pair-setup / pair-verify / TLV8
│   │   ├── screen.c            # AirPlay 屏幕数据流（长度前缀 + 解密/重定钥）
│   │   ├── rtp.c               # RTP/RTCP（音频/后续视频）
│   │   └── stream.c            # SETUP/RECORD/PLAY 与会话注册
│   ├── mfi/
│   │   ├── mfi2c.c             # 2.0C 寄存器状态机（纯逻辑 + 传输回调）
│   │   └── cert.c              # DER 证书最小解析（长度/公钥提取）
│   ├── discovery/
│   │   └── txt.c               # _airplay._tcp / _raop._tcp TXT 生成
│   ├── media/
│   │   ├── h264.c              # AVCC/Annex-B 组装、SPS/PPS 提取
│   │   ├── aac.c               # RTP AAC 解包 / ADTS
│   │   └── clock.c             # PTS、抖动与呈现调度
│   └── crypto/                 # mbedTLS/Monocypher 薄封装（SRP/X25519/Ed25519/ChaCha20-Poly1305/HKDF）
├── port/
│   ├── linux/                  # os(pthread), net(BSD socket), mfi(TCP oracle), bt/fake
│   └── rtthread/               # os(rtthread/aicos), net(SAL), bt(Zephyr BT), mfi(I2C)
├── boards/
│   └── artinchip_d213/         # 板级 wiring：RTL8733、MPP/GE2D、I2S、触摸、LVDS
├── examples/
│   ├── linux_sim/              # 回放协议/单测入口
│   └── d213_app/               # D213 / RT-Thread 应用（当前实现为 rtthread_app）
├── tools/
│   ├── mfi-oracle/             # 主机/板端 MFi 测试桥（如需，单独 GPL-3.0）
│   └── pcap/                   # 抓包解析
└── tests/
    ├── unit/
    └── replay/                 # 录制的 iAP2 / RTSP / MFi 向量
```

> 说明：`boards/` 与 `port/rtthread/` 是**平台适配层**，允许包含 RT-Thread 与 ArtInChip API；
> `core/` 永远不知道它们的存在。

## 4. 公共接口（草案）

接口只暴露"能力"，不暴露具体实现。所有函数返回 `int`，0 成功、负值错误。

### 4.1 OS

```c
struct rtcp_os {
    uint64_t (*now_ms)(void);
    void     (*sleep_ms)(uint32_t ms);
    void    *(*alloc)(size_t size);
    void     (*free)(void *p);
    void     (*log)(int level, const char *tag, const char *fmt, ...);

    rtcp_thread_t (*thread_create)(const char *name,
                                   void (*fn)(void *), void *arg,
                                   size_t stack, int prio);
    rtcp_mutex_t  (*mutex_create)(void);
    void          (*mutex_lock)(rtcp_mutex_t);
    void          (*mutex_unlock)(rtcp_mutex_t);
};
```

core 只在初始化/销毁和少数长生命周期对象上分配内存；热路径使用 `core/common/pool.c` 的固定块池。

### 4.2 网络

```c
struct rtcp_net {
    int  (*listen)(uint16_t port);
    int  (*accept)(int lfd, rtcp_addr_t *peer);
    int  (*connect)(const rtcp_addr_t *addr);
    int  (*send)(int fd, const void *buf, size_t len);
    int  (*recv)(int fd, void *buf, size_t len);
    int  (*poll)(struct rtcp_pollfd *fds, int n, int timeout_ms);
    void (*close)(int fd);
};
```

保留 `poll` 是为了让 core 跑单线程事件循环；Linux 用 `poll(2)`，RT-Thread 用 SAL 的 `select` 适配。

### 4.3 蓝牙 / RFCOMM

```c
struct rtcp_bt {
    int (*enable)(const char *name, const uint8_t bdaddr[6]);
    int (*rfcomm_listen)(uint8_t channel, const char *name,
                         const char *uuid128, uint8_t *out_channel);
    int (*rfcomm_accept)(int lfd, int *fd);
    int (*send)(int fd, const void *buf, size_t len);
    int (*recv)(int fd, void *buf, size_t len);
    void (*set_event_cb)(void (*cb)(int ev, void *user), void *user);
};
```

`ev` 至少包含 connected / disconnected / link-key 请求。SDP 记录的字段由 core 的 `iap2/sdp.c` 生成，平台只负责注册和广播；如果平台 SDP API 限制太大，允许平台构造记录，但记录内容必须来自 core 的模板。

### 4.4 MFi

```c
struct rtcp_mfi {
    int (*info)(struct rtcp_mfi_info *out);   /* id32, protocol_major, cert_len */
    int (*sign)(const uint8_t *challenge, size_t len,
                uint8_t *sig, size_t *sig_len);
    int (*read_cert)(uint8_t *der, size_t *len);
};
```

**约束**：接口没有"导出私钥"，也没有"设置身份"。证书按需读取、用后清零；`read_cert` 可以返回 `RTCP_ENOTSUP`，core 必须能只做签名路径。

### 4.5 Wi-Fi

```c
struct rtcp_wifi {
    int (*ap_start)(const char *ssid, const char *pass, uint8_t channel);
    int (*ap_stop)(void);
    int (*set_vendor_ie)(const uint8_t *ie, size_t len);   /* 可返回 ENOTSUP */
    int (*sta_info)(struct rtcp_sta_info *out);
};
```

`set_vendor_ie` 的字节内容由 core 的 `discovery/ie.c` 定义，平台只负责下发。

### 4.6 媒体与输入

```c
struct rtcp_media {
    int (*video_frame)(const struct rtcp_video_frame *f); /* AVCC/Annex-B + pts */
    int (*audio_pcm)(const struct rtcp_pcm *p);
    int (*audio_config)(uint32_t rate, uint8_t channels, uint8_t bits);
    int (*mic_read)(void *buf, size_t len);
};

struct rtcp_input {
    void (*set_cb)(void (*cb)(const struct rtcp_touch *ev, void *user),
                   void *user);
};
```

解码、显示、I2S 都在 `port`/`boards` 里完成；core 只交付"压缩帧 + 时间戳"和"PCM 块"。

## 5. 线程与事件模型

**推荐：core 单线程事件循环。** 理由：

- 协议状态机天然是单线程的；避免锁和竞态（当前 PoC 的 Timer/对象链表问题就是生命周期竞争的后果）。
- Linux 主机上可以用 `poll()` 精确回放，测试确定性强。
- RT-Thread 上 SAL 支持 `select()`；BT RFCOMM 没有 poll 时，由平台 RX 线程把数据投递到事件队列。

```text
platform threads                core loop (single thread)
───────────────                 ──────────────────────────
BT RX thread  ──event──┐
TCP accept/read ───────┼──►  rtcp_event_queue  ──►  iap2 / airplay state machines
timer tick ────────────┘                                   │
                                                           ├─► media sink (platform, may queue)
                                                           └─► bt/tcp send (platform)
```

`rtcp_event_post()` 是唯一跨线程入口；事件结构固定大小，避免在中断/回调里 malloc。媒体解码和显示在平台侧独立线程，core 通过有界队列交付帧，队列满时丢帧并计数（不阻塞协议线程）。

## 6. 关键设计取舍（需要确认的架构决策）

| # | 取舍 | 选项 | 建议 | 理由 |
|---|------|------|------|------|
| A1 | 许可证 | Apache-2.0 全新实现 / GPL-3.0 复用现有实现 | **GPL-3.0-or-later（已确认，2026-11-08）** | 可复用 `esp32-carplay-web-relay` 的成熟 iAP2/AirPlay 模块；项目已确定开源且允许 GPL；闭源 Realtek blob 单独隔离，不进入仓库 |
| A2 | BT 协议栈 | Zephyr BT / 厂商 ZABlue / BlueZ 移植 | **Zephyr BT**（SDK 内置，Apache-2.0） | 已有 BR/EDR/SDP/RFCOMM/H5；ZABlue 闭源且不可发布；BlueZ 不适合 RT-Thread |
| A3 | 媒体传输 | AirPlay screen DataStream（TCP，长度前缀）/ RTP | **先 DataStream，后 RTP 音频** | 现有开源接收器（esp32-carplay-web-relay）已验证 screen DataStream + 触摸；CarPlay 视频不一定走 RTP，别按 UxPlay 的思路做 |
| A4 | 并发 | 单线程事件循环 / 每连接一线程 | **单线程事件循环** | 可测试、无锁、内存可控；与 RT-Thread 的资源约束匹配 |
| A5 | 抽象粒度 | 每外设一层 HAL / 只抽象跨平台能力 | **只保留 6 个 seam**（os/net/bt/mfi/wifi/media+input） | 避免过度抽象；I2C/SPI/显示寄存器留在平台层 |
| A6 | Rust | 核心 Rust / 可选 Rust 模块 | **C core + 可选 Rust 静态库（C ABI）** | RV64 Rust 工具链与 RT-Thread 集成成本未知；先不阻塞主线 |

> A1 已确认：**core 采用 GPL-3.0-or-later**。因此可以直接移植
> `esp32-carplay-web-relay` 的纯 C 协议模块；从 LIVI 翻译/借鉴的代码必须按其 `NOTICE`
> 保留 "Based on LIVI by Lasse Heitgres" 归因。平台适配层随仓库整体按 GPL-3.0-or-later
> 发布，第三方文件保留各自的许可证；Realtek 二进制与厂商固件仍不进入仓库。

## 7. 平台适配矩阵

| seam | RT-Thread / D213 | Linux 主机（测试） | 未来平台 |
|------|------------------|--------------------|----------|
| os | `rt_thread` / `aicos_*` | pthread | Zephyr / FreeRTOS |
| net | SAL + lwIP | BSD socket / `poll` | lwIP raw / Zephyr net |
| bt | Zephyr BT Classic + H5 | TCP/Unix socket 模拟 RFCOMM | BlueZ / NimBLE |
| mfi | I2C0 0x10，2.0C 寄存器 | LIVI `iap2-mfi` TCP oracle | 其他认证芯片 |
| wifi | Realtek wext / `rt_wlan_*` | hostapd 或 stub | 其他 Wi-Fi 驱动 |
| media | AIC MPP + GE2D + I2S | ffmpeg/SDL/ALSA 或文件 sink | 其他 codec |
| input | 触摸驱动 | 文件/脚本注入 | — |

## 8. 构建与测试

```sh
# Linux host：只构建 core + 单测，不碰任何 SDK
cmake -S . -B build -DRTCP_PLATFORM=linux -DRTCP_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- `tests/replay/` 存放从 PoC/原厂日志中提取的字节流（**不含证书私钥、不含厂商固件**）。
- 每个 core 模块至少一组"正常 + 截断 + 超长 + 非法字段"用例。
- `make check-boundary` 执行第 2 节的 grep，防止平台 API 渗入 core。
- RT-Thread 集成：仓库根提供 `SConscript` 片段和 `Kconfig`；Luban-Lite 应用只需
  `GetDepend('RTCP_USING_CORE')` 并 include `port/rtthread`。core 本身仍用 CMake 独立构建。
- 上述骨架已在 [`rt-carplay`](https://github.com/boa-z/rt-carplay) 仓库落地，
  当前应用入口为 `examples/rtthread_app/`；本 SDK 以 git submodule 形式将其固定在 `packages/third-party/rt-carplay`，并通过 `RTCARPLAY_DIR` 直接引用。

## 9. 分阶段路线（建议）

| 阶段 | 内容 | 退出标准 |
|------|------|----------|
| M0（约 1 周） | 仓库骨架、6 个接口、Linux fake transport、MFi 2.0C 纯逻辑模块 + 单测 | `ctest` 全绿；`check-boundary` 通过 |
| M1（2–3 周） | iAP2 link/session/auth/identify + 录包回放；移植/重写 `esp32-carplay-web-relay` 的纯 C 模块 | 主机上完成一次完整 iAP2 会话回放 |
| M2（2–3 周） | D213 上的 Zephyr BT RFCOMM/SDP + 手机发现/Identify | iPhone 能建立 RFCOMM 并完成 iAP2 会话 |
| M3（2–4 周） | AP/vendor IE/mDNS + AirPlay RTSP/pair-setup/pair-verify | 主机或 iPhone 完成 AirPlay 握手（可无视频） |
| M4（3–4 周） | screen DataStream 解密、H.264 组帧、MPP 解码、LVDS 显示、触摸回传 | 手机上看到画面并能点击 |
| M5 | 音频/麦克风、稳定性、重连、多机型 | 连续运行 24 h 无崩溃/泄漏 |

## 10. 与 Luban-Lite 的关系

- **Luban-Lite 是当前的验证环境，不是 rt-carplay 的依赖。源码以 submodule
  形式随 SDK 检出，但构建边界仍保持独立。**
- 需要在 Luban-Lite 侧保留的只有：
  1. `port/rtthread/` 的实现（OSAL、SAL、Zephyr BT、I2C）；
  2. `boards/artinchip_d213/` 的板级初始化和媒体管线；
  3. 一个最小 `SConscript`/`Kconfig` 接入点。
- RT-Thread 内核补丁（`object.c` / `timer.c` / `wlan_lwip.c` / `wlan_mgnt.c`）属于**内核修复**，
  不进入 rt-carplay core；建议以 `patches/rt-thread/` 或上游 PR 的形式维护。
- Realtek `libwlan_*.a`、Wi-Fi/BT 固件、ZALink/ZABlue 一律不进入 rt-carplay 公开仓库；
  需要它们的构建由用户从厂商 SDK 提供。
