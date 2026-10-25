首次公开发布频谱 · Spectrum，使用 NekoTune SDK 3 PCM 接口。

- 按需分析 FFmpeg 播放音频，提供 48 段频谱与状态事件。
- 通过租约启停采集，无消费者、暂停或退出时释放资源。
- 配合夜光 · Lumen 壁纸显示真实播放频谱；Qt 播放后端不支持 PCM。

安装：下载对应系统的 ZIP，在 NekoTune「扩展」中导入并确认信任。`SHA256SUMS.txt` 用于校验，无需导入。

兼容性：需要 NekoTune v2.0.0 或更新的兼容 SDK 3 宿主，SDK 2.x 宿主无法加载。Linux 使用 Ubuntu 24.04 / GCC 13 / Qt 6.11.2；Windows 使用 MSYS2 UCRT64 / GCC 16 / Qt 6.12.0。原生插件的 Qt 小版本与编译器 ABI 必须和宿主兼容。
