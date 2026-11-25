[English](../ci.md) | [简体中文](ci.md)

# 仓库、固件 CI 与上游

SDK 集成在独立仓库 [boa-z/luban-lite-carplay](https://github.com/boa-z/luban-lite-carplay) 中维护，默认分支为 `carplay`。`codex/androidauto-integration` 与该分支共用最初的 CarPlay 基线。两个分支共用 `boa-z/luban-lite` 的 SDK 快照镜像作为上游。

```sh
git clone --branch carplay --recurse-submodules https://github.com/boa-z/luban-lite-carplay.git
cd luban-lite-carplay
git remote add upstream https://github.com/boa-z/luban-lite.git
git fetch upstream master
```

## SDK 基线与变基

Luban-Lite v1.3.3 基线为镜像提交 `90a740fe9c1b663ea52ec3f7e1af35acde200aea`，对应匠芯创 Gitee 提交 `4bd5a1300f4793a2188529eebde96f012c2188bb`。SDK 基线、固件提交和 `rt-carplay` 子模块提交分别标识。

镜像的 `master` 可能被更新为无共同祖先的新根提交。建议在镜像仓库为每个评审过的 SDK 快照保留以 Gitee 提交号标识的固定标签，便于在 `master` 更新后继续获取。获取更新前，记录 CarPlay 分支的 SDK 根提交和两个产品分支的当前提交。用备份引用或标签保留原分支提交，直到完成验证和发布。在区分大小写的文件系统上使用临时检出目录，例如 WSL 的 Linux 文件系统，便于同时检出上游的两个 README 文件，也能避免构建生成的配置和 Bootloader 写入日常工作区。

```sh
old_carplay=$(git rev-parse carplay)
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

运行 CarPlay 的 Host 回归和 LVGL 预览，构建 D213 Bootloader 与 CarPlay 镜像。变基 Android Auto 分支时，同时构建 `rtprojection` 和 `rtprojection_wireless`。记录 SDK、产品和子模块提交号、defconfig、构建结果及板端验证结果。重写已发布分支前先与协作者协调，确认预期的远端提交后再用带租约保护的强制推送。推送 `carplay` 可能更新滚动 `nightly` 发布。

## 固件构建

在集成仓库执行 `git submodule update --init --recursive` 后运行以下命令。其 `D213 firmware` 工作流验证 PR、推送到 `carplay` 的提交和手动构建。只有 `carplay` 发布 `nightly`。提交信息含 `[ultra-dev]` 时仍上传 Actions 产物，跳过 Release 发布。Git LFS 提供 Xuantie V2.6.1 Linux 工具链和 Realtek 库。

```sh
python3 -m venv .ci-venv
. .ci-venv/bin/activate
pip install -r .ci/requirements.txt
bash .ci/build-firmware.sh
```

Ubuntu 需要安装 `build-essential`、`patch`、`dosfstools`、`mtools`、`device-tree-compiler`、`u-boot-tools` 和 `cpio`。构建脚本编译 `d21x_demo128-nand-carplay_baremetal_bootloader_defconfig` 并逐字节核对打包使用的 Bootloader，再应用 `d21x_demo128-nand_rt-thread_rtcarplay_rtl8733bs_defconfig` 或 `d21x_demo128-nand_rt-thread_rtcarplay_aic8800d40_defconfig`。SDK 生成带模组名的 IMG，CI 检查组件边界和 CRC32。RTL8733BS 还会生成 `ota.cpio`，CI 会核对它和 IMG 的版本及 OS、Rodata 内容，并使用模拟 NAND 验证 SDK OTA 组件。

发布保留 USB IMG 和 [OTA 更新包](ota.md)的 SDK 原始文件名。`SHA256SUMS` 覆盖两份固件和 `source-manifest.txt`，下载后可执行 `sha256sum -c SHA256SUMS` 校验。

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

修改视频链路时执行 `make -C application/rt-thread/rtcarplay check-lubanlite-video SDK_ROOT="$PWD"`，覆盖实际 MPP Sink 和宿主 UI 策略，包括 SDK framebuffer 命令 ABI。还需在[实际面板](single-screen-fusion.md)验证首帧交接、本地界面恢复和触摸。

## 独立 Host 验证

Demo Host 工作流使用 `.ci/dependencies.env` 固定集成仓库、rt-carplay 和 cherryusb-ipheth。工作流核对三个提交，执行格式、双语文档检查、SDK 和 Demo ASan/UBSan 回归以及两个 LVGL 渲染。递归初始化 BSP 子模块，依赖变更完成验证后同步更新三处固定值。

在独立 Demo 检出中用 `SDK_ROOT` 指定固定提交的集成仓库。Host 构建允许通过 `RTCP_ROOT`、`CHERRYUSB_ROOT`、`IPHETH_ROOT` 和 `LVGL_ROOT` 覆盖对应依赖，使用绝对路径。固件中的 `RTCARPLAY_DIR` 可指定外部 rt-carplay 源码，应用只构建选中的 SDK。外部 SDK 的 Kconfig 需兼容集成仓库固定版本。

```sh
export SDK_ROOT=/path/to/luban-lite-carplay
cmake -S tests/launcher_preview -B build/preview -DSDK_ROOT="$SDK_ROOT"
cmake --build build/preview -j8
mkdir -p build/screens
build/preview/launcher_preview build/screens
build/preview/fusion_preview build/screens
```

固件 CI 也让 Demo 的 OTA 工作线程通过模拟 HTTP 和 NAND 读取生成的 CPIO，覆盖校验及非活动 OS/Rodata 写入映射。
