[English](../refactoring-v21-verification.md) | [简体中文](refactoring-v21-verification.md)

# V2.1 架构与验证交接

## 验收边界

可移植 SDK 重构、宿主所有权迁移、生命周期修复、独立 Headless 示例和双语文档已实现并完成软件验证。这是后续自动连接和 Launcher V2 的前置基线。实机验收为 NOT_RUN。此前 38217874 的开发板反馈仅证明开机，并报告 CarPlay 显示异常，不能验证本基线。没有创建 PR。开发与推送使用现有宿主 carplay-oss、SDK main 分支，作者和提交者均为 boa-z。

## 架构与接口

| Owner | 已实现职责 |
| --- | --- |
| SDK include/rtcp | 公共 Receiver 配置、Platform/Media/iAP2 描述、事件、输入、错误码与诊断副本 |
| SDK core 和 runtime | 可移植协议和轮询 Receiver 生命周期，无 GUI 或 SoC 依赖 |
| SDK port/linux 和 port/rtthread | 通用 OS 与网络适配 |
| SDK examples/rtthread_headless | 使用公共 API 和 Null 媒体的独立 Receiver，不是板级应用 |
| 宿主 app | Receiver 组合、Connection Manager、USB/iAP2 所有权、配对持久化和排队命令 |
| 宿主 platform | Realtek/Zephyr/MFi/CherryUSB、MPP/DE、声音输出采集和 GT911 |
| 宿主 ui | LVGL 和帧缓冲可见性策略，连接请求和快照副本 |
| 独立 cherryusb-ipheth | Apple Ethernet 协议、Class 和网络生命周期，不依赖 CarPlay SDK |

Receiver create/start/poll/get_state/stop/destroy 在同一个宿主 Owner 中运行。公共状态分别表示传输、认证、会话、视频、音频和加密输入就绪。编码媒体回调缓冲借用到返回，异步后端复制数据。平台接口表和 Provider 上下文在拆除完成前保持有效。视频就绪表示协议端点，Launcher 切换要求实际显示首帧。安全/HMI 图层策略由宿主持有。参见 SDK 的 Receiver、生命周期和迁移文档，以及[宿主快照](../session-snapshots.md)。

宿主直接使用 SDK 根 SConscript。SDK 正式构建不编译板级示例。Kconfig 符号有唯一 Owner，通用 SDK 和板级选项分离。没有重复协议源码或外部驱动。原有板级 carplay_app API 现在归宿主 app/carplay_app.h 所有，废弃 SDK 示例入口明确报迁移错误。Receiver 保留可选旧配置转换器。Control、分帧、HID 和 Siri 的 Shell 操作排队交给 Poll Owner，跨线程查询返回副本。

## 生命周期审查

| 资源 | 已实现生命周期与回归 |
| --- | --- |
| Receiver 和宿主单例 | 启动失败回滚、重复启停、Owner 重入互斥、状态副本、读者退出和命令队列 |
| 无线 iAP2 | 生命周期串行化、取消代数、持久端点/Provider 存储、拒绝自等待和延迟清理 |
| USB/iAP2 | 命令入口门、拔线代数、Provider 所有权、超时重试期间保留 Stream |
| 触摸 | Reader 退出、停止失败重试、回调有效期和应用输入归属 |
| 音频输出 | 读取计数、生命周期串行化、5 s 超时保留输出资源、拒绝自等待及重复停止清理 |
| 视频输出 | 读取计数、Observer/生命周期串行化、3 s 超时保留解码显示状态、回调互斥和重复停止清理 |
| 麦克风 | 启动取消代数、启停串行化、5 s 超时保留借用网络上下文直到 Owner 退出 |
| 离线音频测试 | Connection Manager 是生命周期 Owner；先停止 Receiver，成功停止测试后才恢复 |
| 配对与文件 | 管理器在删除前暂停连接 Owner，保留身份和不兼容记录 |
| IPHETH | 独立 Class/EP0 回归覆盖重连、取消、接口和网络生命周期模型 |

音频解码、协商、Jitter、Mixer/Direct、重采样、加密麦克风上行和 AEC 策略保持冻结。音视频 Worker 数据路径与前一版本比较一致，生命周期修复在其外围进行。音频路由和零 Stream 问题独立交接至 SDK audio-debug-handoff 文档对。

## 源码与实现记录

| 仓库 | 已发布版本及用途 |
| --- | --- |
| luban-lite-carplay | f2902877e7efd6a7dbbca4da2445cd50cbf1e8c0，最终可执行生命周期基线 |
| rt-carplay | 8947f76297f659d00bb0b32a5d202b86a91f86b6，完整 26 对文档及已测 SDK 基线 |
| rt-carplay | 68158e3，仅同步已完成宿主所有权和测试数量的文档修改 |
| cherryusb-ipheth | 2c87b69fda0d2dbeeb9df5180dd10157791d7371，独立驱动和 10 对文档基线 |

宿主生命周期提交包括 c31cac8a（触摸）、f1c14aa9（音频输出）、3a0fbabe（视频）和 f2902877（麦克风）。SDK 将 36 个板级实现文件和十个板级测试迁至宿主，未重写其媒体、协议行为。后续修改通过实际源码回归修复明确的所有权缺陷。使用 Git name-status 和重命名识别，对照记录的起止版本可取得完整新增、迁移、删除文件清单；[中间实施记录](../refactoring-v21-progress.md)保留迁移过程与原始证据。子模块继续固定到已提交且远端可获取版本。未加入私有认证材料。

## 已执行软件验证

| 检查 | 结果与证据 |
| --- | --- |
| SDK Make | ASan/UBSan、依赖边界、公共 C99/C++11 头文件和文档 PASS |
| SDK CMake/CTest | ASan/UBSan 55/55 PASS，54 个程序及旧配置加密变体 |
| SDK CI | [43642152335](https://github.com/boa-z/rt-carplay/actions/runs/43642152335) 在 8947f762 上 PASS |
| IPHETH | 独立外部 CherryUSB Class/EP0 Debug 与 Sanitizer 3/3 PASS，文档及变异检查 PASS |
| 宿主 Make | 实际组合、媒体、持久化、管理器、USB/无线和生命周期完整 ASan/UBSan PASS |
| 宿主 CI | [43644729058](https://github.com/boa-z/luban-lite-carplay/actions/runs/43644729058) 在 f2902877 上 Host、GUI、Headless PASS |
| GUI/Headless | 使用同源码树匹配裸机 Bootloader 的完整镜像；下载后独立核对每种镜像五项 SHA256 清单及九项 IMG 组件边界、CRC32 |
| 通用 Headless 示例 | 固件集成 COMPILED_NOT_STARTED，独立行为用 Host 调度模型测试 |
| D213 硬件 | 本源码和镜像为 NOT_RUN，不推断显示、音频、重连或 DMA 验收 |

| 基线镜像 | 字节数 | SHA256 |
| --- | --- | --- |
| GUI | 4788736 | 87ddb69931c278ecc37d887666e412acb67e7414e1f4283bb500eb2b87f981d6 |
| Headless | 3987968 | 9b2a5762b6670d862837e5eb2876e002cae1744994b4198f6f68ba9d9c17b8a2 |

镜像身份仅对应 f2902877 和 SDK 8947f762。后续文档或 Launcher 修改需要独立 CI 和镜像身份。本地原始日志、行为冻结比较及下载清单保留在忽略目录 output/ci-logs 和 output/ci-verified-43644729058。可用 SDK make check、Sanitizer CMake/CTest、独立 IPHETH CMake/CTest、宿主 make -C application/rt-thread/rtcarplay -j8 check 及 [CI](ci.md) 中 GUI/Headless 工作流命令复现。

## 双语文档审计

SDK 有 26 对完整英中文档，覆盖 README、架构、Receiver API、生命周期、宿主迁移、音频子系统和交接、已有音频研究、USB 认证/持久化/视频调查、无线/Bond、性能、RT-Thread Port/示例及音频 Fixture/依赖归属。IPHETH 有十对完整文档：根 README、示例 README、架构、兼容性、延迟、移植、协议审计、来源、验证和文档审计。各仓库检查器自动发现维护文档，检查命名、双向导航、内部链接/锚点、语言、单行段落、章节结构、代码块、内联标识符和外部引用一致性。两套检查器各通过 17 项隔离变异测试并运行于 CI。

SDK 仅豁免 third_party/ed25519/UPSTREAM.md，保留上游来源记录。原许可证和第三方法律声明保持不变。IPHETH 没有维护文档豁免。翻译审查保留历史状态和原命令、代码、测量结果；机械一致性检查不能证明语义完全等价。SDK 检查器输出维护文档对数量，Git 可列出准确当前清单。[英中文 SDK 文档](../../../../../packages/third-party/rt-carplay/README.md)和独立 IPHETH README 提供双向导航。

## 剩余验证与后续工作

物理 USB/无线仲裁、Trust/Bond 持久化、重连、首个可见帧和 Launcher 恢复、sound0/dmic0、触摸、失败回滚及长时间压力测试仍为 NOT_RUN。此前 framebuffer ioctl ABI 缺陷已有实际源码回归和软件修复，面板可见行为需要新一轮开发板测试。ASan/UBSan 和调度模型不能证明不存在所有硬件或跨线程竞态。

自动同时发现、单活动连接策略及 Launcher V2 是后续应用修改，必须保留这些 SDK/驱动边界和音频行为。授权的历史分支、仓库清理属于独立管理操作，需要刷新并验证备份，本交接未执行。交付没有创建 PR、创建分支或切换分支。
