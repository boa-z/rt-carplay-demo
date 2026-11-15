[English](../mbedtls-upgrade-assessment.md) | [简体中文](mbedtls-upgrade-assessment.md)

# mbedTLS 4.x 评估

## 建议

可以将 4.1 LTS 作为安全及长期维护迁移目标，目前不能把它视为已经证实的 D213 性能升级。官方 4.1.1 发布说明承诺至少支持到 2029 年 3 月。当前测试镜像没有升级库，优先完成有线启动修复及验证。实际嵌入式源码是 2.28.1，旧包选择标签不能准确代表源码版本。

## 测量证据

已按照官方公布的 SHA256 校验 4.1.1 发布包，独立解压，并用 RV64 Linux 编译器和 QEMU 执行类型探针。其附带的密码实现默认仍在 RV64 上选择 32 位 MPI 单元，因此直接升级不会自动获得本轮对现有库实施的架构修正。使用私有头文件的探针仅供评估，应用不能依赖该内部 API。

| 检查 | 结果 | 限制 |
| --- | --- | --- |
| 当前库，原生通用 MPI，32/64 位完整配对生成 | 586.8 / 163.9 ms CPU 时间 | 单次确定性测试，不是版本比较 |
| 当前库，RV64 QEMU，32/64 位完整配对生成 | 2166.4 / 816.9 ms CPU 时间 | 单次模拟执行，不是 D213 墙钟时间 |
| 用户测试的优化后 D213 镜像 | 配对记录生成 6933 ms | 缺少同条件修改前实机基线 |
| 官方 4.1.1 默认 RV64 MPI 探针 | 32 位单元 | 类型选择检查，不是速度基准 |
| D213 上 2.28.1 对比 4.1.1 | NOT_RUN | 无法给出可靠的版本升级提速百分比 |

这些生成测试保留两把独立 RSA-2048 密钥及证书生成。RSA 素数搜索耗时随随机输入变化，不能把单次测试比例当作预期实机加速比。用户日志将生成时间与信任弹窗等待分开记录，但没有测量各个生成子阶段的 CPU 占用。

## 迁移成本与兼容性

4.x 将密码实现拆分为 TF-PSA-Crypto，以 PSA 为主要密码 API，并以 CMake 为受支持构建系统。应用当前使用底层 MPI/RSA、ChaCha20-Poly1305、HKDF 等旧接口，配对记录构造及协议密码操作需要适配公开 PSA 接口。旧密码 ALT 后端被 PSA 驱动替代，现有 ArtInChip AES/SHA 加速需要专门移植；如果丢失加速，部分操作可能更慢。熵初始化、线程、密钥生命周期、内存分配及 SCons 集成也需调整。

AirPlay SRP-6a 也使用旧 MPI 接口。PSA 不提供通用 MPI 算术的直接替代，需要选择受支持的 SRP 实现或单独维护运算后端，并完成协议向量及配对回归；依赖密码库私有头文件不能形成稳定迁移方案。

当前 Lockdown 客户端限定 TLS 1.2。4.x 删除 TLS 1.2 中的静态 RSA 及有限域 DHE 密钥交换，但仍支持 ECDHE 中的 RSA 签名。现有日志没有显示手机最终选择的密码套件，迁移前必须采集实际协商结果并进行 iPhone 兼容测试。不能假定 TLS 1.3 的改进会加速只使用 TLS 1.2 的连接。

## 预期性能范围

RSA 生成变快会缩短首次配对，而缓存重连原本就会跳过生成。持续 CarPlay 音视频还依赖 USB/Wi-Fi 调度、缓冲、AAC 解码、重采样及显示，替换 TLS 库不会直接修复这些路径。密码库发布说明提到 AArch64 Neon 的 ChaCha20 提升约 2x 到 2.7x，这是特定架构的证据，不适用于本项目 D213 RISC-V 目标。没有依据承诺帧率、音乐质量或整机 CPU 的通用提升百分比。

迁移前应在同一开发板上，对优化后的现有库与选定的最新 4.1 维护版本进行比较，保持安全检查、熵策略和硬件加速条件等价。重复测量 RSA 生成、首次及缓存 Lockdown、协商套件、小包 ChaCha20-Poly1305、SHA/HKDF、固件体积、堆栈峰值及同时播放音视频，分别报告中位数、尾延迟、初始化成本及实机验收。

## 官方参考

- [4.1.1 发布说明及官方发布包](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-4.1.1)
- [4.0 迁移指南](https://github.com/Mbed-TLS/mbedtls/blob/mbedtls-4.0.0/docs/4.0-migration-guide.md)
- [密码 API 迁移](https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/tf-psa-crypto-1.1.1/docs/1.0-migration-guide.md)
- [固定版本密码库 MPI 类型选择](https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/tf-psa-crypto-1.1.1/drivers/builtin/include/mbedtls/private/bignum.h)
- [固定版本密码库发布说明](https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/tf-psa-crypto-1.1.1/ChangeLog)
