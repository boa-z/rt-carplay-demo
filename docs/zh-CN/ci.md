[English](../ci.md) | [简体中文](ci.md)

# 仓库、固件 CI 与上游

SDK 集成的维护仓库为 [boa-z/luban-lite-carplay](https://github.com/boa-z/luban-lite-carplay)，这是独立仓库，开发分支和默认分支均为 `carplay-oss`。仓库以 `boa-z/luban-lite` 提交 `4060f85bd27a1eb555952921139b77cc41bdeb4e` 为基线，将既有公开 CarPlay 集成记录为独立提交，不引入 `jiachenin/luban-lite-jc` 的其他开发历史。

```sh
git clone --branch carplay-oss --recurse-submodules https://github.com/boa-z/luban-lite-carplay.git
cd luban-lite-carplay
git remote add upstream https://github.com/boa-z/luban-lite.git
git fetch upstream master
```

上游默认分支是 `master`。获取上游更新不会自动修改集成分支或固定的子模块版本。合入 SDK 更新前应审查差异，并构建两个固件变体。仓库不会自动创建 PR。

## 固件构建

`D213 firmware` GitHub Actions 工作流在推送到 `carplay-oss` 时运行，也支持手动触发。GUI 和 Headless 在独立的 Ubuntu 24.04 任务中构建。检出启用 Git LFS 和递归子模块；SDK 通过 LFS 保存 Xuantie V2.6.1 Linux 工具链压缩包及所需 Realtek 库。

```sh
python3 -m venv .ci-venv
. .ci-venv/bin/activate
pip install -r .ci/requirements.txt
bash .ci/build-firmware.sh gui
# 另一个变体应使用独立的干净检出。
bash .ci/build-firmware.sh headless
```

Ubuntu 需要安装 `build-essential`、`patch`、`dosfstools`、`mtools`、`device-tree-compiler` 和 `u-boot-tools`。构建脚本首先从本仓库编译 `d21x_demo128-nand-carplay_baremetal_bootloader_defconfig`，并逐字节核对打包使用的 Bootloader 与新构建二进制一致，再应用所选 RT-Thread defconfig，生成完整 ArtInChip `.img` 容器。此流程不依赖预编译 Bootloader 或另一个 SDK 检出。

产物包含 `d213-carplay-gui.img` 或 `d213-carplay-headless.img`、匹配的 Bootloader、固件配置、解析后的镜像布局、源码与子模块版本，以及 `SHA256SUMS`。下载并解压对应工作流产物，在使用开发板的 ArtInChip 工具烧录前执行 `sha256sum -c SHA256SUMS`。这些镜像对应 D213 `demo128-nand-carplay` 及现有 128 MiB SPI NAND 板卡与面板配置，使用时应选择正确变体和板卡。

产物保留 14 天，失败时上传构建日志。工作流成功表示交叉编译和打包通过；实际 USB 升级、NAND 启动、面板、音频和 iPhone 验收需要另行上板，源码清单中记录为 `NOT_RUN`。

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
