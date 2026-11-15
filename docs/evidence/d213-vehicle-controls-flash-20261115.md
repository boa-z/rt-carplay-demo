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

## Acceptance at this checkpoint

| Level | Status | Scope |
| --- | --- | --- |
| L0 | PASS, initial source research | Existing LIVI/code/test and official public material report. |
| L1 | PARTIAL for full scope | Portable protocol foundations have Host evidence; complete official Ultra inventory, rendering and integration remain incomplete. |
| L2 | NOT_RUN for full acceptance | New GUI flashed and identity restoration verified; visual fusion, touch boundaries, disconnect/reconnect, faults and measured performance still need physical validation. |
| L3 | NOT_RUN | Real-iPhone Ultra negotiation on this image. |
| L4 | NOT_RUN | Real-iPhone Vehicle State/UI Sync exchanges. |
| L5 | NOT_RUN | Real-iPhone independent Type 111 reception. |

Next steps are to confirm the new runtime revision and shell after a normal power cycle if necessary, verify identity files after restart, observe FULLSCREEN/FUSION/LOCAL, test phone/local touch boundaries and reconnect, and capture media/resource statistics. Do not infer official Ultra support from local instruments, a phone prompt, Host tests or the old firmware's video latch.

## 中文说明

本次 GUI/Headless 镜像的集成版本为 `363c7e0c`，SDK 为 `d0a15bb`；Host、GUI、Headless CI 均通过，下载产物的版本、SHA256、镜像边界/CRC32 及配置/ELF 检查均通过。SDK 后续 `0692be5` 只增加响应背压测试及中英文说明，不改变以上固件的构建身份；本地完整 Make 和启用 sanitizer 的 CMake 72/72 测试通过。

用户断电重启后，旧固件返回 FUSION 已生效、视频已锁存、错误码为 0，但尚无实体屏幕和触摸验收。两次新导出的 6 个配对/身份文件共 51,252 字节，SHA256 全部一致。aiburn 刷入以上 GUI 镜像并返回成功，启动 CRC 通过、NFTL OK；同时启动分类器仍记录 USB 枚举错误、Shell 未就绪，独立 upgrade_complete 字段为 false，因此不能宣称完整正常启动。ADB 已恢复可用。

6 个配对/身份文件已恢复并回读，全部 SHA256 与本次刷前备份一致。已发送串口 reset 请求重新加载，请求之后再次导出仍与刷前 6 个文件全部匹配；但后续串口查询仍没有有效响应，不能据发送成功认定重启已经执行，也不能据文件恢复认定自动回连。备份、身份、原始日志均保留在 Git 之外，蓝牙 bond 持久化尚未验证。

当前镜像仅启用由手机主动提供时才协商的模拟发动机 RPM Vehicle State 模型，不注册控制命令，也不声明尚未接入的 UI Sync、HEVC 或第二显示屏能力。完整 L1 仍为 PARTIAL，L2–L5 仍为 NOT_RUN；下一步需确认新固件运行版本，观察三种显示模式和触摸边界，验证断连/重连、异常恢复及性能，再记录真实 iPhone 协商结果。本地仪表融合和 Host 测试均不等于完整官方 Ultra。
