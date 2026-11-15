# 有线 USB 模式激活排查

[English](../wired-mode-activation.md)

## 失败边界与历史对照

失败日志中 USBMUX 配对、CarKit TLS、MFi 认证和 identification 已完成，但 Apple Ethernet 接收帧为零，OUT 传输超时且实际传输字节为零。本地有效 IPv6 和应用策略置 link-up 不代表手机已接受网络通信，问题发生在 AirPlay 之前，不能根据这些日志归因于画面渲染或音乐解码。

最后由用户确认能够启动并出画面的镜像是 `output/wired-invite-recovery/gui/d213-carplay-gui.img`，SHA256 为 `a1efd44e3e714d7a9ab106a2263284105c1f3994726e66924eb24083ed0e2b51`。核对冻结的功能性 C、头文件、补丁、defconfig 和 SConscript，规范化换行后与主仓库 `9da899012eb5f180e35819201eedd96b392e5a87`、IPHETH `ed797d2a89b0e4b060b9f9e650f8356ded852609` 一致。这个镜像后来出现音乐死机和卡顿，只能作为连接启动对照，不能称为稳定版。成功日志在 MFi 完成前已有 Ethernet RX/TX，但缺少开机 USB 枚举部分，无法确认成功时的 USB 模式。

| 后续变更 | 影响范围 | 现有证据能说明什么 |
| --- | --- | --- |
| `7784b996` | EHCI 异步描述符安全回收 | 两种后续安全 IRQ 顺序都失败，但没有与原始不安全实现完成受控对比，不能据此定位坏提交。 |
| `6e950099` | RX 独立锁及 lwIP 排队输入 | 改变调度和资源生命周期，没有改变 Apple MAC/NCM 请求编码。 |
| `fa9439c0`、`50cca15d` | 主机/会话就绪策略及网络定时器预算 | 可以解除本地 DAD 等待，但实测零字节 OUT 超时仍存在，本地就绪不等于手机就绪。 |
| `a0ef1ec9` | 手机保持连接时的重试及 USB 文件传输集成 | 保留了早期配置选择的短路判断。 |

SDK 始终固定在 `894575c161ffcfaa5862a06858bb254bf2c03776`。目前没有证据将实机回归唯一归因于某个提交，也没有依据撤回内存安全修复。

## 应用层修正

原来只要扫描到 USBMUX + Apple Ethernet 配置就直接返回，跳过 `0xc0/0x52`、value=0、index=4、回复一字节的模式请求。因此普通网络共享接口也会阻止模式激活。最后可用版本已经存在这个判断：本轮证明的是潜在的策略缺口，不能把它说成已确认的新回归根因。

社区参考 [DiPlay 615b32ce](https://github.com/shihabal3amri/DiPlay/tree/615b32ce0b40cd0b99ce94ba23a512001553684f) 的 `IphoneCarPlayConfiguration.kt`、`IphoneUsbHost.kt`、`WiredConfigurationAction.kt` 和 `CarPlayController.kt` 区分普通可用 Ethernet 与观测到的 USBMUX + CDC-NCM + Apple Ethernet 组合模式。这是社区兼容性经验，不是 Apple 官方规范，本轮未复制参考项目源码。

现在应用优先选择该组合模式；只有普通 Ethernet 时，在驱动绑定前发送一次模式请求，并在原有五秒有限窗口内重新读取设备和配置描述符。配置索引和值由描述符决定，不写死。请求被拒绝或回复长度不对时保留兼容配置回退；请求完成但描述符不变化时，窗口结束后使用重新读取的兼容配置；设备描述符连续读取失败则丢弃旧回退，避免选择消失设备的配置。未编译 CDC-NCM 驱动时不选择仅有 CDC-NCM 的配置。回复一字节只证明 USB 请求完成，不能当成 Ethernet 或 AirPlay 可用。

修改仅在应用 USB 适配层，没有修改 SDK、IPHETH、EHCI、lwIP、音视频链路或并行 OTA 工作。DiPlay 还提示已结束会话可能在保留完整组合描述符时仍需重新激活；当前首次枚举策略尚未证明或解决这个场景。

## 验证和下一次实机日志

Host 测试编译真实应用适配源码，使用合成描述符模拟手机模式切换。旧实现提前选择普通 Ethernet 且请求次数为零，选择断言失败。修正后的 ASan/UBSan 测试覆盖模式切换后重新选择、完整模式已存在、请求拒绝、短回复、描述符不变及设备失去响应；原有 DMA、取消传输和开机已连接手机的所有者测试也通过。这些测试证明软件决策，不证明真实 iOS 行为；测试中的配置数量和索引不是手机必须满足的值。

Linux/WSL 可运行 `make -C application/rt-thread/rtcarplay check-lubanlite-usb BUILD=/tmp/carplay-usb-check`。GUI 镜像、冻结源码清单、构建日志和验证报告位于 `output/wired-regression-deep/`，未生成 Headless 固件。源码快照保留了并行未提交的 OTA/UI 工作，因此镜像不是仅由 Git 已提交内容构建。

实机验收仍为 NOT_RUN：本轮访问 AiBurn `127.0.0.1:9100` 被拒绝，未烧录。请保留完整开机枚举日志，重点检查模式请求实际字节数、更新后的描述符及选择索引、MAC/NCM 初始化、MFi 前后的 RX/TX、IPv6、StartSession、入站 RTSP 和第一帧。iPhone 接 USB1 Host，分别验证手机开机前已连接、拔插连接、会话结束后不拔线重试。若新选择流程下仍出现零字节 OUT 超时，保留取消前 EHCI 快照，并在相同手机状态下对照精确旧镜像。音乐死机、卡顿、无线回归和 PC 文件传输仍需单独实机验证。

## 后续验收与异常恢复

用户随后报告开启 iPhone 蜂窝数据后有线连接正常，并明确确认音乐播放正常。有线连接和音乐更新为用户实测 PASS，具体受测镜像哈希未提供。这不能证明所有有线 CarPlay 都必须开启蜂窝数据，也不能证明连接恢复由模式选择修改导致。有线界面卡顿仍为待解决项。

本轮修正应用工作线程的两条失败路径：真实 iAP2 LINK_DEAD 事件只请求线程退出，不在回调内部销毁自身会话；工作线程销毁会话、释放自己安装的 MFi provider 后才公布静止状态，由现有连接管理器关闭流并重试。通用 iAP2 读取助手用零同时表示处理成功和 EOF，应用现在检查原始 CarKit 读取结果，将正数字节交给原 SDK 会话，并在收到数据或空闲超时时推进定时器。EOF 和 I/O 错误退出，普通空闲超时继续等待，无需修改 SDK API 或协议。

真实工作线程的 ASan/UBSan 回归在修正前复现了收到 peer RESET 后仍继续读取的问题。修正后经真实 SDK 会话和实际工作线程，分别运行“空闲后 RESET”、EOF、I/O 错误各 32 轮，验证 provider 只释放一次、会话清理完成及重复 stop 安全。保留线程创建/启动失败、拔插代次、自等待和停止超时保留资源测试。可运行 `make -C application/rt-thread/rtcarplay check-usb-iap2-worker`。清理播放循环内的周期性音频栈扫描和日志，保留停止时及显式音频测试的栈水位；保留必要控制器快照及回归测试，删除旧版源码替换测试入口。

此前 CPU 异常仍独立保留：只有故障镜像匹配 `output/wired-regression-deep/gui/firmware.elf` 时，`0x4011c5d8` 才对应 lwIP `mld6_tmr` 的 `mld6.c:504`，返回地址 `0x4012898c` 对应 `lwip_cyclic_timer`。故障指令通过无效 IPv6 组播链表指针读取半字。该 ELF 的 42 个编译单元 netif 布局一致，因此未观察到此镜像的结构布局不一致，但尚未找到内存破坏源。不能把工作线程修正称为此崩溃的根因修复。后续需要核对精确烧录镜像并保留异常寄存器、接口生命周期证据，复测失败恢复，不通过屏蔽异常、跳过任意链表节点或关闭 IPv6 组播掩盖问题。

下一阶段用同一场景对照有线和无线，复用按需开启的 `carplay_perf on`/`off`、`carplay_video status`、`ipheth_status`，关联网络/锁等待、RX 停顿与视频排队时长、解码、显示提交、丢 NAL 和 IDR 重建，再决定是否调整调度或缓冲。音乐正常并不能确定画面卡顿发生在哪一段。
