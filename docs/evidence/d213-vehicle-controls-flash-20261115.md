# D213 Vehicle State foundation: CI and flash checkpoint

## Reproducible image identity

| Item | Evidence |
| --- | --- |
| Integration revision used to build images | `363c7e0c2fc60dbccfbb0eb8ae24789d55652249` |
| SDK revision used to build images | `d0a15bbd4312df82dca208ef3cb2b519cc91c357` |
| Integration CI | [43773366483](https://github.com/boa-z/luban-lite-carplay/actions/runs/43773366483): Host, GUI and Headless passed. |
| SDK CI | [43773237669](https://github.com/boa-z/rt-carplay/actions/runs/43773237669): sanitizer Host and bilingual documentation passed. |
| GUI image SHA256 | `a536a4b2aaf5c279d9a65cdca9453401ad2f2d41c6172d7d7188020b177c58de` |
| Headless image SHA256 | `8c251615af497fce8c479c16f9c5b94b72196684d46db6f95525fda6140080b4` |
| Local artifact verification | All three artifacts downloaded; both firmware artifacts passed six SHA256 checks, exact source pins, clean source inventories, nine component bounds/CRC32 checks and config/ELF wired-profile checks. |
| Test-only SDK follow-up | `0692be5`: adds actual Receiver response-retention tests and bilingual documentation; no production source changes. Complete local SDK Make and sanitized CMake 72/72 tests passed with this change. The follow-up passed [SDK CI 43774136015](https://github.com/boa-z/rt-carplay/actions/runs/43774136015). Images above retain their original build revisions. |
| Publication | Rolling Preview Release skipped; no Release published. |

## Device operations and limitations

The user power-cycled D213 before this flash. The existing firmware then answered `carplay_display status` with requested=FUSION, effective=FUSION, generation=1, latched=1, video=1 and error=0. This proves a sampled coordinator/video state on the previous firmware; it does not establish visible composition, correct touch or new-image acceptance. ADB file transfer was available.

Two independent fresh `/data` exports contained six identity/pairing files totaling 51,252 bytes. All SHA256 values matched each other and the earlier verified backup. The GUI image above was flashed through the project aiburn skill. The tool returned success and exit code 0, and its classifier reported five target partitions written. Its separate upgrade_complete field was false; no stronger completion claim is inferred from that field. Boot CRC passed and NFTL was reported OK, but the classifier recorded USB enumeration errors and no ready shell. ADB returned after flashing.

All six identity/pairing files were restored and read back; every SHA256 value matched the fresh pre-flash export. A serial reset request was sent to reload them. A second export after this request again matched all six files. Later serial reads and the correct display-status command produced no runtime response. Successful command transmission does not prove execution or successful reboot. ADB remained available. Physical display, phone state, automatic reconnection and the cause of intermittent serial silence remain unverified.

Raw backups, manifests and logs remain outside Git in the private device-backup directory. No phone identities, pairing secrets, keys or proprietary theme assets are included here. No Bluetooth settings file existed in the six-file backup, so Bluetooth bond persistence is not established.

The image includes the explicitly simulated engine RPM Vehicle State model, enabled only when offered by the phone. The model registers no typed service controls. UI Sync, HEVC, alternate-display declarations and the experimental Type 111 Receiver gate remain disabled in these D213 profiles. The software control tests do not imply phone-side command compatibility or physical vehicle actuation.

## Subsequent runtime observations and visual failure

Later aiburn commands with explicit leading carriage return and CRLF received valid shell responses. This is a useful command convention for this session, not proof of the root cause of earlier silence. `carplay_ota status` confirmed the running version as `Firmware 1.0.0 (363c7e0c)`.

The real phone offered Vehicle State, the Receiver enabled it, and Normal/HIGH both reported state 2 (connected). The simulated engine model reported RPM=1000 and sample_error=0. The processor counted one request and zero rejections; delivery counted one queued response, zero retries/rejections/abandonment and error=0. There were no subscriptions or notifications. The current counters do not identify the request method or prove phone receipt/acceptance, so do not infer verified configuration, reads/writes, subscription semantics or complete Vehicle State interoperability. UI Sync remains disabled. This is real-phone negotiation and bounded application-processing evidence, distinct from the earlier Host results and complete official Ultra.

Mode commands selected LOCAL, FULLSCREEN and FUSION without a reported coordinator error. Immediately returned status can show the prior video generation; a later FUSION sample confirmed requested generation=6 and latched generation=6, video=1, error=0, rectangle `(0,75,768,450)` and logical input `1024x600`. The local sample was marked SIMULATED with speed=64 and battery=82. Read-only DE queries showed:

| Mode | Video layer | UI layer |
| --- | --- | --- |
| FUSION | Enabled; destination 768x450 at (0,75); decoded 1024x608 buffer cropped to 1024x600. | Enabled; 1024x600 buffer, stride 4096, crop (768,0,256,600), position (768,0). |
| LOCAL | Enabled behind UI; destination 1024x600 at (0,0). | Enabled; full 1024x600, origin (0,0), crop disabled. |
| FULLSCREEN | Enabled; destination 1024x600 at (0,0). | Full geometry restored; global alpha enabled, mode=1, value=0. |

Video status reported 3,117 frames, zero drops/errors/resets, queue peak one, no IDR wait or skipped NALs, and 7.1 FPS over a 214,921 ms interval. Average/max receiver stage wall times were queue=0/21 ms, decode=9/13 ms and present=8/17 ms. The phone workload was not controlled, so this is neither a frame-rate benchmark nor phone-to-glass latency. One `top` sample returned CPU=50; heap collection produced no usable output, so no memory result is claimed.

Despite those software/driver observations, **the user reported that the left CarPlay region was still black**. Persistent black video with an active session is a visual failure, not expected fusion behavior. FULLSCREEN was then selected again and its UI alpha=0 confirmed; the user confirmed the full-screen video was normal. FUSION was restored and `test_fb -A -e -m 0 -v 255` selected pixel alpha through the existing driver command. A separate readback confirmed alpha enabled, mode=0 and value=255. The user then confirmed that the left video displayed. This isolates the visible failure to the FUSION alpha policy: DE initializes the UI surface to full panel size (`drv_de_core.c`), and a cropped rectangle with global alpha 255 obscures the video's region. The application fix uses pixel alpha for FUSION and retains original LOCAL/global-zero FULLSCREEN behavior. No video copy or BSP change is needed.

The restored picture is a runtime modification of the image identified above; a new image with the source fix, reboot persistence, right instruments, physical touch, disconnect/reconnect and fault recovery still need validation. Driver configuration and frame counters alone did not prove visible success, and the original image's visual failure remains part of this record.

## Acceptance at this checkpoint

| Level | Status | Scope |
| --- | --- | --- |
| L0 | PASS, initial source research | Existing LIVI/code/test and official public material report. |
| L1 | PARTIAL for full scope | Portable protocol foundations have Host evidence; complete official Ultra inventory, rendering and integration remain incomplete. |
| L2 | PARTIAL, runtime fix | Original image failed with black left video; user confirms FULLSCREEN normal and FUSION video restored by runtime pixel alpha. New fixed image, right instruments, touch, disconnect/reconnect and faults remain unverified. |
| L3 | PARTIAL | Real phone offered/enabled Vehicle State and connected both lanes. UI Sync, remaining Ultra negotiation and official qualification remain NOT_RUN. |
| L4 | PARTIAL, bounded observation | One real-phone request processed without rejection and a response queued; method, phone receipt, read/write/subscription/notification acceptance and UI Sync remain unverified. |
| L5 | NOT_RUN | Real-iPhone independent Type 111 reception. |

Next steps are to build and flash the pixel-alpha fix after a fresh verified pairing backup, revalidate visible composition and mode changes after reboot, test phone/local touch boundaries and reconnect, identify real Vehicle State requests and responses without exposing phone/identity data, and collect controlled media/resource statistics. Do not infer official Ultra support from local instruments, a phone prompt, Host tests or a video latch.

## 中文说明

本次 GUI/Headless 镜像的集成版本为 `363c7e0c`，SDK 为 `d0a15bb`；Host、GUI、Headless CI 均通过，下载产物的版本、SHA256、镜像边界/CRC32 及配置/ELF 检查均通过。SDK 后续 `0692be5` 只增加响应背压测试及中英文说明，不改变以上固件的构建身份；本地完整 Make 和启用 sanitizer 的 CMake 72/72 测试通过。

用户断电重启后，旧固件返回 FUSION 已生效、视频已锁存、错误码为 0，但尚无实体屏幕和触摸验收。两次新导出的 6 个配对/身份文件共 51,252 字节，SHA256 全部一致。aiburn 刷入以上 GUI 镜像并返回成功，启动 CRC 通过、NFTL OK；同时启动分类器仍记录 USB 枚举错误、Shell 未就绪，独立 upgrade_complete 字段为 false，因此不能宣称完整正常启动。ADB 已恢复可用。

6 个配对/身份文件已恢复并回读，全部 SHA256 与本次刷前备份一致。已发送串口 reset 请求重新加载，请求之后再次导出仍与刷前 6 个文件全部匹配；但后续串口查询仍没有有效响应，不能据发送成功认定重启已经执行，也不能据文件恢复认定自动回连。备份、身份、原始日志均保留在 Git 之外，蓝牙 bond 持久化尚未验证。

后续使用显式 CRLF 收到有效串口响应，运行版本确认是 `363c7e0c`。真实手机已提供并启用 Vehicle State，Normal/HIGH 两条通道连接，处理器收到 1 条请求、0 拒绝，响应入队且无重试/错误；尚无订阅或通知，计数器也不能确定请求方法、手机收到响应或接受其语义。模拟模型不注册控制命令，也不声明尚未接入的 UI Sync、HEVC 或第二显示屏能力。

三种模式命令及硬件图层查询已有记录，FUSION 的后续采样确认代次/锁存均为 6、视频有效、错误码 0；DE 视频层为左侧 768×450、位置 (0,75)，UI 图层为右侧 256×600、位置 (768,0)。用户明确报告左侧持续黑屏后，再次切到 FULLSCREEN 并确认 UI 全局透明度为 0，用户确认全屏视频正常。随后切回 FUSION，通过现有 `test_fb -A -e -m 0 -v 255` 改为逐像素 alpha，独立回读确认 mode=0、value=255，用户确认左侧恢复显示。因此问题定位到 FUSION 的 alpha 策略：DE UI 平面仍为整屏，裁剪矩形配合全局 alpha 255 遮住视频区域。修复仅在应用层 FUSION 使用逐像素 alpha，LOCAL 和 FULLSCREEN 保留原有策略，不增加视频拷贝或修改 BSP。

以上恢复是原镜像上的运行时试验，尚不证明固化修复的新镜像、重启、右侧仪表、触摸、断连/重连和异常恢复通过；原镜像的黑屏失败也不能从记录中抹去。L2 据此记为 PARTIAL，而非完整通过。

视频统计为累计 3,117 帧、0 丢帧/错误/重置、队列峰值 1；214,921 ms 区间为 7.1 FPS，队列/解码/呈现平均耗时为 0/9/8 ms，单次 CPU 样本为 50。手机负载未受控，不能作为性能基准或端到端延迟；内存未取得有效结果。L0 仍 PASS，完整 L1 为 PARTIAL；L3 和 L4 根据以上真实手机的有限证据记为 PARTIAL，其他未测项及 L5 仍 NOT_RUN。下一步先生成并验证固化 alpha 修复的镜像，再完成输入、重连和真实协议交互验收。本地仪表、Host 测试及单项协商均不等于完整官方 Ultra。
