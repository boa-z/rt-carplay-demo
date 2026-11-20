[English](../ci.md) | [简体中文](ci.md)

# 仓库、固件 CI 与上游

SDK 集成在独立仓库 [boa-z/luban-lite-carplay](https://github.com/boa-z/luban-lite-carplay) 中维护，默认分支为 `carplay-oss`。`codex/androidauto-integration` 在 `carplay-oss` 上继续集成 Android Auto。两个分支共用 `boa-z/luban-lite` 的 SDK 快照镜像作为上游。

```sh
git clone --branch carplay-oss --recurse-submodules https://github.com/boa-z/luban-lite-carplay.git
cd luban-lite-carplay
git remote add upstream https://github.com/boa-z/luban-lite.git
git fetch upstream master
```

## SDK 基线与变基

2026 年 11 月 20 日，本次变基将 SDK 基线从 `4060f85bd27a1eb555952921139b77cc41bdeb4e` 更新为 `90a740fe9c1b663ea52ec3f7e1af35acde200aea`。两个快照均标记为 Luban-Lite v1.3.3，新快照对应匠芯创 Gitee 提交 `4bd5a1300f4793a2188529eebde96f012c2188bb`。构建记录和评审说明使用完整的镜像及 Gitee 提交号标识 SDK 版本。

镜像的 `master` 可能被更新为无共同祖先的新根提交。建议在镜像仓库为每个评审过的 SDK 快照保留以 Gitee 提交号标识的固定标签，便于在 `master` 更新后继续获取。获取更新前，记录 CarPlay 分支的 SDK 根提交和两个产品分支的当前提交。用备份引用或标签保留原分支提交，直到完成验证和发布。在区分大小写的文件系统上使用临时检出目录，例如 WSL 的 Linux 文件系统，便于同时检出上游的两个 README 文件，也能避免构建生成的配置和 Bootloader 写入日常工作区。

```sh
old_carplay=$(git rev-parse carplay-oss)
old_projection=$(git rev-parse codex/androidauto-integration)
old_sdk=$(git rev-list --max-parents=0 "$old_carplay")
git merge-base --is-ancestor "$old_carplay" "$old_projection"
git fetch upstream master
new_sdk=$(git rev-parse upstream/master)
git log -1 --format='%H %s' "$new_sdk"
git diff --stat "$old_sdk" "$new_sdk"
```

确认 `old_sdk` 只有一个提交、祖先关系检查通过，且 `new_sdk` 是预期的上游快照。找到 `old_sdk` 之后的独立 README 兼容提交并核对其变更。如果新快照仍同时包含 `README.md` 和 `ReadMe.md`，在临时检出目录准备新基线：

```sh
git switch --detach "$new_sdk"
git mv ReadMe.md README.zh-CN.md
git commit -m 'chore: rename ReadMe.md to README.zh-CN.md to fix case conflict'
new_base=$(git rev-parse HEAD)
```

将 `old_base` 和 `new_base` 分别设置为旧、新 README 兼容提交。在某个快照无需兼容提交时，使用该 SDK 快照提交作为边界。记录两个边界后，在临时检出目录按依赖顺序重放产品提交：

```sh
git switch --detach "$old_carplay"
git rebase --no-update-refs --onto "$new_base" "$old_base"
new_carplay=$(git rev-parse HEAD)
git switch --detach "$old_projection"
git rebase --no-update-refs --onto "$new_carplay" "$old_carplay"
new_projection=$(git rev-parse HEAD)
git range-diff "$old_base..$old_carplay" "$new_base..$new_carplay"
git range-diff "$old_carplay..$old_projection" "$new_carplay..$new_projection"
```

核对新 SDK 是 `new_carplay` 的祖先，`new_carplay` 是 `new_projection` 的祖先。检查 `range-diff` 中有变化或被跳过的补丁，确认两个分支都包含 `README.md` 和 `README.zh-CN.md`，并逐一核对变基前后的子模块 gitlink。构建前初始化锁定版本的子模块并取回 Git LFS 文件。需要升级子模块时，使用独立的产品提交。

运行 CarPlay 的 Host 回归和 LVGL 预览，构建 D213 Bootloader 与 CarPlay 镜像。变基 Android Auto 分支时，同时构建 `rtprojection` 和 `rtprojection_wireless`。记录 SDK、产品和子模块提交号、defconfig、构建结果及板端验证结果。重写已发布分支前先与协作者协调，确认预期的远端提交后再用带租约保护的强制推送。推送 `carplay-oss` 可能更新滚动 `preview` 发布。

## 固件构建

`D213 firmware` GitHub Actions 工作流在推送到 `carplay-oss` 时运行，也支持手动触发。它先运行 SDK 与应用 Host 回归及两个实际 LVGL 预览，再构建唯一的统一固件，然后把生成的 IMG、A/B OTA 包和 Bootloader 作为滚动 `preview` GitHub Release 发布。提交信息含 `[ultra-dev]` 的推送只跳过发布步骤：镜像仍会构建并作为 Actions 产物上传供复核。检出启用 Git LFS 和递归子模块；SDK 通过 LFS 保存 Xuantie V2.6.1 Linux 工具链压缩包及所需 Realtek 库。

```sh
python3 -m venv .ci-venv
. .ci-venv/bin/activate
pip install -r .ci/requirements.txt
bash .ci/build-firmware.sh
```

Ubuntu 需要安装 `build-essential`、`patch`、`dosfstools`、`mtools`、`device-tree-compiler`、`u-boot-tools` 和 `cpio`。构建脚本首先从本仓库编译 `d21x_demo128-nand-carplay_baremetal_bootloader_defconfig`，并逐字节核对打包使用的 Bootloader 与新构建二进制一致，再应用 `d21x_demo128-nand_rt-thread_rtcarplay_defconfig`，生成完整 ArtInChip `.img` 容器。此流程不依赖预编译 Bootloader 或另一个 SDK 检出。上传前，`.ci/verify-image.py` 会核对容器边界、全部九个组件的 CRC32，以及必需的 DDR 升级器、Bootloader 和 OS 记录。

工作流将 `d213-carplay.img`、A/B OTA 包 `ota.cpio`、匹配的 Bootloader、`version.txt`（Settings 读取的清单，含 `version=`、`commit=`、`board=`、`size=` 与 `sha256=`）、`source-manifest.txt` 和 `SHA256SUMS` 发布到滚动 `preview` 预发布版本。从该 Release 下载后，在使用开发板的 ArtInChip 工具烧录前执行 `sha256sum -c SHA256SUMS`。这些镜像对应 D213 `demo128-nand-carplay` 及现有 128 MiB SPI NAND 板卡与面板配置。OTA 包在各次构建之间保持固定 URL。

失败时上传构建日志。工作流成功表示交叉编译和打包通过；实际 USB 升级、NAND 启动、面板、音频和 iPhone 验收需要另行上板，源码清单中记录为 `NOT_RUN`。

## CherryUSB 裸机集成

`packages/third-party/cherryusb-luban` 中的 SDK 适配层提供可与中断安全交互的信号量和有界队列、前台定时器、Hub 轮询、SDK 升级兼容函数及 ArtInChip 升级描述符回调。适配使用 SDK 的内存、时间、临界区和延时接口，不模拟创建线程。生成的 Hub 构建副本仅在 `KERNEL_BAREMETAL` 下以 `usbh_hub_poll()` 替代线程，RT-Thread 使用的 Hub 源码保持原样。

同步 EHCI 传输等待期间保持中断开启，超时返回 `-USB_ERR_TIMEOUT`。前台 Hub 轮询处理连接通知和设备枚举。定时器回调在前台执行，IRQ 可以启动或停止定时器。定时器创建、删除及 Hub 轮询必须由单一前台所有者执行；删除定时器或同步对象前，必须停止产生完成通知的资源。Hub 反初始化先释放子设备接口，再停止控制器并释放队列和信号量。

Bootloader 保留既有 USB Device 升级、U 盘升级及 SD 卡升级选项，其手动 USB 角色策略与应用的自动 OTG 策略分离。应用主循环仍需调用 `usbh_hub_poll()` 及既有 OTG 轮询入口。此轮询适配不提供 RTOS 线程、RTOS 类示例或任意后台任务。

```sh
bash packages/third-party/cherryusb-luban/tests/run_baremetal.sh
bash packages/third-party/cherryusb-luban/tests/run.sh
bash packages/third-party/cherryusb-luban/tests/run_control_probe.sh
```

测试使用 ASan/UBSan 编译真实适配源码，验证软件契约，不模拟开发板枚举或烧录成功。

## 视频显示回归

修改视频链路时执行 `make -C application/rt-thread/rtcarplay check-lubanlite-video SDK_ROOT="$PWD"`，覆盖实际 MPP Sink 和宿主 UI 策略，包括 SDK framebuffer 命令 ABI。已确认的命令不匹配和板端验收要求见 [视频交接修复](video-handoff-fix.md)，固件身份及更正后的用户反馈见 [迁移验证报告](repository-migration-verification.md)。

## 宿主持有的平台回归测试集

获取 SDK 固定版本的 mbedTLS 后，在 ASan/UBSan 下执行 `make -C application/rt-thread/rtcarplay -j8 check`，包括迁移的 Receiver、音频、配对测试，以及已有视频、触摸、启动页、Bond、USB DMA、Handoff、重连和能力测试。SDK 独立测试集不再编译板级源码。通用 SDK 开关只从其 Kconfig Source 一次，硬件选择与源码收集由应用负责。参见 SDK [迁移说明](../../../../../packages/third-party/rt-carplay/docs/zh-CN/luban-lite-migration.md)。这些验证使用 Host 设备、调度模型，不是实机验收。

CI 校验完整 CRC CPIO 归档，并将写入映射限制为 OS 与 rodata 后才生成授权清单。清单在其它资产之后上传；滚动发布替换期间发生的不一致会在大小或 SHA256 校验时失败，需要重新检查更新。详见 [OTA](ota.md)。
