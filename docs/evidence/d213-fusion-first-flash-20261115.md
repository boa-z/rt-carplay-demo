# D213 first fusion firmware flash — 2026-11-15

This checkpoint records a development flash and incomplete physical acceptance. It does not establish real-iPhone Ultra negotiation, successful CarPlay fusion presentation or official Ultra capability.

| Item | Evidence |
| --- | --- |
| Integration | `1b96d3f8914c7bae6c9c5d70dfd95327b9f94785` |
| SDK | `e175a8f019d5c86b5e152397fb0a75d758d11668` |
| CI | [43754025640](https://github.com/boa-z/luban-lite-carplay/actions/runs/43754025640), Host + GUI + Headless SUCCESS |
| GUI image SHA256 | `370c98d25d333e1251d94ba7d02989f3520f11b1bd0157753617565a368b6eb7` |
| Headless image SHA256 | `2491d7c99f6efdc86f6ffcaaace41366b4d489c0dc69c5116df0cea33281cea2` |
| Download verification | All six checksums per firmware, exact integration/SDK pins, empty source-change inventories and nine component bounds/CRC checks per image passed. |
| Preview Release | Skipped by `[ultra-dev]`; no Release published. |
| Device | D213, 1024×600; COM9 at 115200; GUI image flashed through project aiburn entry point. |

## Backup, flash and source identity

Two fresh independent `/data` reads matched all six prior pairing/identity files, totaling 51,252 bytes. Another immediate pre-flash read also matched. Raw files and manifests remain outside Git under restricted private-backup permissions; no credentials or phone identifiers are included here.

The first aiburn invocation stopped before writing because its Windows GBK stdout could not encode a status symbol. Running the same entry point with UTF-8 completed the flash. The CLI returned success. Its boot-log classifier reported USB errors and did not establish a ready shell; subsequent serial commands did reach the running application. Do not turn the classifier into a clean-boot claim. A later firmware status query returned `Firmware 1.0.0 (1b96d3f8)`.

The complete image cleared `/data`; its first read contained only a newly generated 32-byte seed. All six old files were restored through the existing USB file-sync service, read back and SHA256 verified. A reset was requested to reload them. Another subsequent read matched every original file again. This verifies file restoration, not automatic reconnection or Bluetooth bond persistence. No Bluetooth settings file existed in the backup.

## Observed application state

Serial commands reached the new coordinator and changed LOCAL/FUSION requests. Two sampled status responses showed different simulated speeds, gear D and battery 82%, with no display error. The DE query showed an enabled, uncropped 1024×600 UI layer at origin, stride 4096, and disabled video. This is application and driver-state evidence; the physical panel, rendered pixels and touch behavior have not been independently observed.

```text
Firmware 1.0.0 (1b96d3f8), slot ?, remote ()
[carplay-display] requested=LOCAL effective=LOCAL generation=2 latched=0 video=0 error=0
[carplay-display] requested=FUSION effective=LOCAL generation=3 latched=0 video=0 error=0
```

No decoded/latching video frame was observed. The automatic USB owner repeatedly failed during pairing with `ret=-1` (`RTCP_EIO`), operation `OpenLockdown`, zero exchanges and `paired=0`. The attached device remained reported as present. Temporarily disabling runtime wireless competition did not restore wired reception. Wireless was re-enabled and the default FUSION request restored afterward. With no video the effective mode remains LOCAL.

The log's `trust cache=new load=-6` does not prove that restored files are absent or invalid: this code also uses `RTCP_ENOENT` when the preliminary UniqueDeviceID/Lockdown connection needed to select a cache path fails. File readback matched the backup. The phone's lock/permission state, USB reconnection state and exact cause of the failed connection are not established. Physical phone reconnection/unlock may be needed to continue diagnosis; that has not been performed. Do not infer an iPhone compatibility regression or successful automatic reconnect from this evidence alone.

Serial collection intermittently returned no output amid large radio initialization logs, then resumed and accepted mode/status commands. No monitor was explicitly stopped. Local driver/query success is not a clean long-duration stability result. Raw flash, restore, boot, connection and mode logs remain in the private local evidence directory; this document includes only non-sensitive excerpts.

## Acceptance

| Level | Status | Scope |
| --- | --- | --- |
| L0 | PASS, initial source research | Existing LIVI/code/test and official-public-material report. |
| L1 | PARTIAL | Portable foundations and gated Vehicle State declarations have Host evidence; full inventory/rendering and remaining Ultra integration are incomplete. |
| L2 | NOT_RUN for full acceptance | Flash, file restoration and limited LOCAL state checks performed. Physical video/UI fusion, local/phone touch, reconnect, fault recovery and performance remain unverified. |
| L3 | NOT_RUN | Real-iPhone Ultra negotiation. |
| L4 | NOT_RUN | Real-iPhone Vehicle State/UI Sync interaction. |
| L5 | NOT_RUN | Independent Type 111 reception. |

Next physical checks: establish wired Classic reception using the restored identity; record actual DE video and cropped UI layers in FUSION; observe all three modes on the panel; test phone/local touch boundaries and disconnect/reconnect; capture resource and frame/stage statistics. Complete official Ultra additionally requires the separate codec, renderer/theme, protocol, wireless and applicable official integration/qualification work already recorded in the development report.

## 中文说明

本次已刷入可追溯的 `1b96d3f8` GUI 镜像，Host/GUI/Headless CI 全部成功。刷写清空了 `/data`，原 6 个配对/身份文件已恢复，恢复后及后续重启请求后的回读 SHA256 均与刷前备份一致；这不等于已验证自动回连。备份未包含蓝牙 settings 文件，蓝牙 bond 持久化仍未验证。

新固件能报告并切换 LOCAL/FUSION 请求，模拟速度持续变化，DE 显示独立 UI 层启用、视频层关闭。但有线会话在 usbmux/Lockdown 建连阶段失败，未收到视频帧；关闭无线竞争仍未恢复，随后已恢复无线策略和 FUSION 请求。`cache=new load=-6` 也可能来自读取设备标识前的建连失败，不能据此认定配对文件丢失。手机锁屏/权限、USB 重连状态及根因尚未确认，可能需要手机解锁或物理重插后继续诊断。

尚未独立观察实体屏幕、视频合成与触摸效果，不能将已刷写或 LOCAL 状态查询当作 L2 通过。L2 完整验收及 L3–L5 均保持 NOT_RUN，也未宣称官方 Ultra 能力。串口一度无输出后恢复；烧录器的启动日志分类报告 USB 错误，后续命令可用，因此也不宣称完整稳定启动已通过。私有原始日志和配对材料均未提交。
