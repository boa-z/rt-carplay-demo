[English](../launcher-network-verification.md) | [简体中文](launcher-network-verification.md)

# Launcher Ethernet 验证

## 源码与行为

本地交付在现有 `carplay-oss` 分支扩展 Launcher V2，基于 `b933ef756afd6c9498d99d016482693b45f048b0` 加未提交的互联网共享实现，基础提交本身不能标识镜像。未创建分支、PR、推送或操作远端。操作见 [Launcher V2](launcher-v2.md)，路由与协议边界见[互联网共享](internet-sharing.md)。此前[互联网验证](internet-verification.md)记录上一版镜像，本报告标识增加 Launcher 功能后的镜像。

首页 Ethernet 入口打开实际 LVGL 网络页，包含网卡/链路/地址/网关/DNS、配置的 HTTP 204 目标、进度/结果/时间、非阻塞 Check now 和独立共享状态。WAN 检测证据不再依赖 AP 是否可用，WAN 变化使其失效，AP 变化仅影响转发。现有单个网络 Worker 处理自动和手动检查。未修改可移植 SDK、音视频或 CarPlay 协议行为。

## 已执行的软件验证

| 检查 | 结果 | 证据与边界 |
| --- | --- | --- |
| 完整 Host 回归 | PASS | 应用 Makefile check，在 ASan/UBSan 下使用 BUILD=build-network-validation；实际连接、媒体、输入、USB、无线/Bond 与恢复代码，设备边界使用模型。新构建目录避免复制的 CMake 缓存残留原绝对源码路径。 |
| 生产 lwIP 路由与实际 Supervisor | PASS | ASan/UBSan 下两个 CTest 可执行程序，包含无 AP 的独立 WAN 成功、恢复转发、网络快照、合并手动请求、旧结果拒绝、链路/地址变化、HTTP 失败和证据过期。RT-Thread 与 Socket 边界使用模型。 |
| 真实 LVGL 渲染与事件 | PASS | 十二张 1024×600 预览，网络不可用、未插线、检测中、成功与失败；缺少网关、过期、按钮禁用/请求路由、返回和视频接管；既有设置/设备/音频/配对交互。输入为模型，不是开发板测量。已目视检查首页和联网页 PNG。 |
| GUI 完整镜像 | PASS | 原 GUI defconfig 启用 Ethernet/共享，匹配缓存的同源码 Bootloader、链接网络代码，九个 IMG 组件边界/CRC 通过。 |
| Headless 完整镜像 | PASS | 原 Headless defconfig 禁用共享、匹配 Bootloader、无 NAPT 符号，九个 IMG 组件边界/CRC 通过。二进制与上版 Headless 交付一致。 |
| 文档与静态检查 | PASS | 成对文档标题/代码/技术字面量/链接/段落、SConscript AST、工作流矩阵与 git diff 空白检查。 |
| 开发板与 iPhone 验收 | NOT_RUN | 未烧录或执行硬件测试。Ethernet DHCP/PHY、手动检测、拔线/网络故障恢复、触摸响应与 CarPlay 并发负载仍需实机验证。iPhone 采用网关需独立路由证据。 |
| 远端 CI | NOT_RUN | 仅执行本地构建与测试。 |

## 可复现命令

```sh
make -C application/rt-thread/rtcarplay BUILD=build-network-validation -j8 check
cmake -S application/rt-thread/rtcarplay/tests/internet_share -B build/network-check -DCMAKE_BUILD_TYPE=Debug
cmake --build build/network-check -j8
ctest --test-dir build/network-check --output-on-failure
cmake -S application/rt-thread/rtcarplay/tests/launcher_preview -B build/launcher-preview
cmake --build build/launcher-preview -j8
mkdir -p output/launcher-network-preview
build/launcher-preview/launcher_preview output/launcher-network-preview
```

## 产物身份

产物位于 `output/launcher-network-validation/`。gui 和 headless 目录包含完整 IMG、firmware.config、bootloader.bin、image-layout.json、firmware-symbols.txt 与 SHA256SUMS。交付还包含 PNG/PPM 预览、原始日志、模型 lwIP 路由抓包、changed-source.zip、local-changes.patch、source-manifest.json 与 ARTIFACT-SHA256SUMS。清单记录每个修改源码的哈希及子模块身份，SDK 和 IPHETH 子模块保持干净且未修改。构建在逐字节匹配的 WSL ext4 副本顺序执行，避免 Windows 文件系统构建开销；指定检出仍选中 GUI 配置。

| 方案 | IMG SHA256 |
| --- | --- |
| GUI | `9724728af97e932aef1e243aeae881f856c57b080410bdb028acb52c43a5770d` |
| Headless | `1335f773c7e8716b98294f1eb27f82647f26d39ad5d110a4d0a90282514c24c3` |

默认连通性目标可配置。失败可能是目标受阻、DNS、超时或认证门户，不代表全部互联网目标不可达；该检测不能证明 iPhone 可上网。实机先不连手机：开机、进入 Ethernet 页面、获取 DHCP 租约、点击 Check now、拔插网线，观察旧成功清除并重新检测，再在无线和有线 CarPlay 期间重复，记录本镜像哈希和原始日志。
