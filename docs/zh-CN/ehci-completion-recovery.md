[English](../ehci-completion-recovery.md) | [简体中文](ehci-completion-recovery.md)

# EHCI 完成回收修复

## 实机证据

用户确认，有线音乐播放读访问异常发生在已交付的 wired-invite-recovery/gui/d213-carplay-gui.img（SHA256 a1efd44e3e714d7a9ab106a2263284105c1f3994726e66924eb24083ed0e2b51）上。异常报告为 mcause=5、mepc=0x40069a22、mtval=0x0000006f216000d8、ra=0x4006b722。符号表将 PC 定位在 ehci_urb_waitup，返回地址位于 USBH_IRQHandler，但 IMG 在该 PC 处的指令为分支而非读指令。这种不一致尚未解释，不能据此将根因归到确切指令。音频栈遥测的观测峰值为使用 1876/98304 字节，不能证明音频栈耗尽。有线画面卡顿也仍为已报告的问题。

## 已复现的软件缺陷

EHCI 异步推进确认（IAA）对应其发生之前的队列移除操作。原中断函数先扫描 INT/ERR 完成位，期间可能移除另一项已完成的 QH 并请求新的异步推进，随后却用入口采样到的 IAA 释放所有待回收 QH。当完成中断与旧 IAA 同时到达时，新移除的 QH 会在自身确认到达前被释放，可能提前复用 DMA 描述符并交付回调。这是已复现的生命周期缺陷，尚未证明它是实机死机或卡顿的唯一根因。

本地构建补丁调整为先处理采样到的 IAA，再扫描 INT/ERR。扫描过程中新增的移除操作继续等待后续确认。描述符缓存处理、既有短包修复、协议能力、SDK 接口和无线策略保持此前行为，上游 CherryUSB 检出未修改。

## 回归与验证范围

Host 回归直接执行真实补丁后的 USBH_IRQHandler，构造一个旧的待回收 QH 和第二个已完成传输，分别组合 IAA 与 INT、ERR、INT 加 ERR，要求本次中断只完成旧传输，再由后续 IAA 恰好一次完成新传输。修复前，INT 场景的回调计数断言失败；修复后，三种组合及既有短包、零长度、完整传输、错误、控制状态阶段和缓存一致性测试均通过 ASan/UBSan。负向测试日志随新镜像验证产物保留在本地。

```sh
bash packages/third-party/cherryusb-luban/tests/run.sh
bash packages/third-party/cherryusb-luban/tests/run_control_probe.sh
```

Host 结果不能证明物理 USB 控制器缓存行为、播放稳定性或流畅度。新 GUI/Headless 构建结果和镜像身份以配套产物验证报告为准。后续实机需要重复有线音乐和投屏、断开重连及 USB/Wireless 切换，并验证无线音视频回归。既有 lwIP 定时器池耗尽、音频时间戳缺口和实机回声验收仍为独立未完成事项。不宣称新镜像已通过实机验收。
