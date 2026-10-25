# 频谱 · Spectrum

独立原生扩展 `nekotune.spectrum`（2.0.0），需要提供 SDK 3（插件接口 `org.nekotune.IExtensionPlugin/3`）的 NekoTune；PCM 能力由宿主播放后端决定，默认 FFmpeg 后端支持。
只分析播放器默认 FFmpeg 后端的实际输出，不采集系统声音、不启动第二个播放器。
Qt 兼容播放后端可启用扩展，但状态会明确显示频谱不可用；SDK 2.x 旧宿主无法加载本版本。

## 构建与安装

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

默认使用 `third_party/nekotune-sdk` 的 SDK 3 源码快照；也可用
`-DNEKOTUNE_SDK_SOURCE_DIR=/path/to/NekoTune/sdk` 或 `CMAKE_PREFIX_PATH` 指向目标宿主 SDK。
必须使用与宿主相同的 Qt 小版本和编译器主版本；Windows 使用 MSYS2 UCRT64 / GCC 16 / Qt 6.12.0。
源码快照只提供接口，不代表任意已发布宿主已具备 PCM 实现。

在宿主扩展管理器挂载 `build/extensions/nekotune.spectrum`，或安装 `build/packages/` 下的 ZIP，
确认信任并启用。Linux、Windows 各自构建本机库；版本标签 CI 在 Linux 和 Windows 测试通过后，生成含两个平台 ZIP 与 SHA-256 校验文件的草稿 Release。
扩展设置页显示播放后端、消费者数量、可用性和分析状态。

## 主题接入协议 v1

主题通过 `api.call("extensions.call", {id: "nekotune.spectrum", service, params})` 调用：

| 服务 | 参数及返回 |
| --- | --- |
| `status` | 返回 `version, available, backend, reason, consumers, playing, analyzing, bands:48, hz:30` |
| `acquire` | 返回状态，以及随机 `token`、`ttlMs:15000`；最多 64 个消费者 |
| `renew` | 参数 `{token}`，续期 15 秒；建议每 5 秒续期，过期令牌返回错误 |
| `release` | 参数 `{token}`，可重复释放 |

`extension.nekotune.spectrum.status` 事件的 `data` 是上述状态。
`extension.nekotune.spectrum.frame` 的 `data` 为
`{version:1, generation, sequence, positionMs, bands:[48个0～1数值]}`。
主题过滤事件名与协议版本，丢弃旧序号、旧代次和非法数据，超过 500ms 无有效帧时归零。
Qt 的 QVariantList 在 QML 中不一定满足 Array.isArray；按长度及逐项数值校验后复制到 JS 数组。
暂停、隐藏、断线、减少动态效果或销毁主题时释放租约，重连时重新申请。
原始 PCM 不出后端进程，不写配置或磁盘。

## 分析与生命周期

48 个对数频段覆盖 40Hz～min(16kHz, Nyquist)，Hann 窗长至少约 85ms，采用二次幂 FFT。
分别分析左右声道后合成功率，反相立体声不会相消；使用固定 -72～0dB 映射、快速上升和缓慢回落。
不自动放大静音噪声。跨淡化展示实际混音，音量为零时逐步归零。

多个屏幕共用一个采样订阅、一个分析线程。PCM 待处理队列最多 250ms，输出邮箱只保留最新结果；
缺帧、采样率或流代次变化重置 FFT。无消费者、暂停或后端不可用时停止工作线程及 PCM 订阅。
租约检查周期 33ms；被销毁但未成功释放的视图最多保留至租约失效。

测试覆盖频率、静音、反相立体声、多采样率、格式异常、缺帧重置、租约到期、旧宿主、
真实插件加载、多个消费者共享、暂停、后端回退和停用清理。
宿主额外集成测试使用 `NEKOTUNE_TEST_SPECTRUM_EXTENSION_DIR=/path/to/build/extensions/nekotune.spectrum`
运行 `nekotune_pcm_capture_test`，无需用户配置或账号。
Windows CI 配方已提供；真机和实际执行结果见交付记录，不以 CI 配方代替验收。
