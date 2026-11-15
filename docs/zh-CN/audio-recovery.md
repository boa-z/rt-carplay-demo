[English](../audio-recovery.md) | [简体中文](audio-recovery.md)

# 通话回声与音乐播放恢复

本文记录较早的音频修复基线。用户随后实测报告音乐异常和通话回声仍存在，本文 Host PASS 不能作为实机验收。替换实现、自动延迟对齐和新验证流程见[媒体与传输修复](media-transport-repair.md)。

## 范围与依据

用户反馈无线电话/Siri 播放正常、音乐异常，以及通话对方听到回声。这分别属于下行媒体和上行声学问题。上一版 GUI 配置启用了麦克风，但关闭 AEC。D213 HAL 的采样率设置会同时改变播放与麦克风时钟。此前 Sink 只要发现部分 RTP 时间戳重叠就丢弃整个已解码包，媒体时钟补偿也没有设置目标水位。这些事实已经通过源码确认；尚无手机抓包能证明音乐失真的唯一原因。

本次修改限于 Luban-Lite 应用与原有 GUI defconfig，不修改可移植 SDK、协议能力、编解码器实现或子模块版本。实现基于提交 838eef85，保留 Ethernet 共享及已验证的 PHY 时钟配置。

## 通话回声

GUI 配置启用已有的定点 SpeexDSP AEC。播放 Worker 提供混音/增益后的双声道 PCM，并查询 sound0 已有的排队字节接口。参考历史根据待播放样本估计 DAC 播放时间，保持连续的 10 ms 块，并支持跨块、块内偏移读取。独占 dmic0 适配器记录 DMA 到达时间并扣除采集积压，因此采集线程晚运行时不会直接按当前墙钟匹配。缺失参考样本时跳过消除并增加诊断计数，不使用随意选取的旧帧。

麦克风 Worker 独占 AEC 状态。运行时模式、延迟变化和采集时间不连续会重建滤波器，不修改 UDP 序号或加密状态。保留降噪，AEC 请求开启时关闭额外 RMS AGC，避免继续放大残余回声。播放/采集队列补偿后的默认残余延迟为零，这不代表完成装机声学校准。已有自适应滤波器覆盖约 100 ms。采集 Pipe 改为 4096 字节，同时使 SDK 双声道 48 kHz 采集 DMA 周期变为约 21.3 ms；播放内存池增至四块。这些有界缓冲以少量延迟换取调度余量。

```text
carplay_mic
carplay_mic aec off
carplay_mic aec on
carplay_mic delay 0
carplay_mic delay 10
carplay_mic mute
carplay_mic unmute
```

AEC 开关和延迟由下一次采集读取应用，请求模式与延迟在 RAM 中跨会话保留，重启后不保留。延迟接受 0 至 200 ms，表示额外声学/参考偏移，不是网络往返时间。从零开始，设置超过实际回声延迟可能使效果变差。在对方持续讲话时比较 ref_ok、ref_miss、ref_drop、process_max_us、capture_gaps、peak 和 clipped 计数。启动或空闲时少量参考缺失是预期情况；持续缺失、削顶或处理耗时超过 10 ms 帧周期，应先排查再调参。

## 音乐播放

默认混音输出将共享硬件时钟保持为 48 kHz。麦克风运行时，即使请求原采样率直出也会让出通路，防止 44.1 kHz 播放改变固定 48 kHz 的采集时钟。此保护也阻止原采样率 PCM Probe 回放改变正在运行的麦克风时钟。

部分重叠的解码包保留尚未播放的后半段，完整过期的包仍丢弃。诊断将 overlap_trimmed 与 negative_ts 分开记录。媒体通道启用既有的有界 Mixer 时钟补偿，目标水位为 40 ms。102/media 混音通路在启动或恢复时等待约 40 ms PCM，有限尾部最多等待 80 ms 后续数据；语音和本地提示不受该媒体预缓冲条件限制。保留已有认证、包拒绝、编解码格式及本地关键声音抢占。

```text
carplay_status
carplay_audio_path mixer
carplay_audio_path direct
carplay_audio_probe capture
carplay_audio_probe status
carplay_audio_probe replay
```

比较实际 last_output，而非仅看请求的通路。PCM Probe 会改变播放行为，仅用于诊断；麦克风占用 48 kHz 时钟时不能进行原生 44.1 kHz 回放。录取与回放之间不要断开 Receiver，会话拆除会释放 Probe。对同一音乐片段记录播放前及播放中的 decoded、decode_err、negative_ts、overlap_trimmed、silence_frames、late、overflow、queue_busy 和 device_errors。

## 验证与实机验收

Host 测试覆盖实际采集适配器、AEC 开关两种麦克风 Worker、真实 AAC 的混音及直接输出、部分重叠处理、共享时钟保护和输出参考传递。带时间戳的合成回声测试检查跨块对齐、回声衰减及双方同时发声时独立近端信号的保留。虚拟设备和合成信号不能证明 D213 的声学效果或音乐音质。

```sh
make -C application/rt-thread/rtcarplay BUILD=build-network-validation -j8 check
```

开发板上保持适中且一致的扬声器音量，让对方使用耳机，比较 AEC 开关。分别测试仅对方讲话、仅车机端讲话和双方同时讲话，记录清晰度、残余回声和诊断。随后用同一首已下载音乐测试无线及有线 CarPlay、电话/Siri 打断后恢复音乐，以及 Ethernet 共享并发，保留既有有线卡顿/音质问题作为对照。真实回声改善、音乐改善、CPU 余量和持续全双工稳定性在刷入替换镜像验证前均为 NOT_RUN。本地验证不涉及新建 PR、分支或远端发布。

## 本地交付

| 检查 | 结果 | 依据 |
| --- | --- | --- |
| 完整应用 Host 套件 | PASS | ASan/UBSan 下运行实际源码，含连接、媒体、输入、无线/配对、USB 和生命周期回归。 |
| AEC 与采集新增测试 | PASS | AEC 开关两种 Worker、DMA/积压时间戳与回滚、跨块参考、运行时参数校验、合成回声及独立近端双讲。稳定后的合成残余能量比约为 0.000521，不是声学测量。 |
| 音乐与参考传递 | PASS | 真实 AAC 默认混音及直出、部分重叠后半段保留、麦克风运行时的时钟保护和排队参考传递。 |
| Ethernet 共享 | PASS | 实际 lwIP 路由和应用管理器两个 CTest 可执行测试，设备边界使用模型。 |
| 实际 LVGL 渲染/事件 | PASS | 12 张 1024x600 预览及既有网络、设置、设备、音频、配对交互。 |
| GUI/Headless 完整镜像 | PASS | 9 个 IMG 组件通过边界及 CRC32 校验，使用同源缓存 Bootloader；Headless 与前一交付物逐字节一致。 |
| 开发板/iPhone 验收 | NOT_RUN | 需要用户测试此次新镜像；此前 Ethernet/iPhone 成功属于上一镜像。 |

GUI 镜像为 output/audio-repair-validation/gui/d213-carplay-gui.img，SHA256 为 `082ea3bd8cc9b8e0944c17f2aa09cb8cdbd694f9896da51b5d32c80e08d0464d`。Headless SHA256 为 `1110fe5b33c64c323cbefb2cea665755809055458d039518b158a0bc82b5af6b`。目录包含准确配置、镜像布局、符号、Bootloader、构建/Host 日志、修改源码快照、二进制补丁和校验和。源码清单记录基线提交 838eef85 与本次本地修改，指定工作区保留交付 GUI 配置。
