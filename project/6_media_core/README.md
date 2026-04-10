# Media Core 设计评审

## 总体架构

本模块将编解码流程拆成可替换组件，形成 `pipeline + interfaces + ffmpeg adapters` 三层：

- `interfaces`：定义稳定抽象（Demuxer/Decoder/Converter/Encoder/Muxer/Probe）。
- `ffmpeg adapters`：对 FFmpeg 的最小封装，业务不直接散落 FFmpeg API。
- `pipeline`：编排端到端流程（读包 -> 解码 -> 转换 -> 编码 -> 写封装 -> flush）。

这样后续接入 WebRTC 时，可保留 `Decoder/Converter/Encoder`，仅替换输入输出端（采集源/RTP 发送）。

## 目录结构

- `include/media_core/common`：状态码、RAII
- `include/media_core/config`：转码配置
- `include/media_core/interfaces`：组件抽象
- `include/media_core/factory`：组件工厂
- `include/media_core/pipeline`：编排器
- `src`：FFmpeg 适配实现
- `demo`：本地文件转码示例

## 硬解能力探测与回退策略

首版通过 `AVCodecHWConfig` 进行能力探测，按优先级尝试硬件类型（如 VideoToolbox/NVDEC/QSV/VAAPI），失败自动回退软解：

1. 根据目标解码器枚举可用 `AVHWDeviceType`。
2. 匹配配置中期望的 `preferred_hw_device`。
3. 若设备创建失败或解码流程失败，记录日志并回退软解。

## WebRTC 复用约束

- 编解码模块不得依赖传输协议对象。
- 输出包统一抽象为 `EncodedPacket`（可扩展元数据：关键帧、时间戳、codec extradata）。
- 预留运行期动态参数接口（码率、分辨率、fps、强制关键帧）。

## 代码骨架说明

本仓库提供：

- 可编译的接口/实现骨架；
- 一个视频转码 demo（输入文件 -> 输出文件）；
- 严格 flush 流程（decoder + encoder + muxer trailer）。

当前阶段聚焦视频主链路，音频接口作为扩展点预留。

## 在 Cursor 中单独打开

如果在大工作区里出现“定义可见但无法跳转”的情况，推荐直接单独打开 `project/6_media_core`，或者打开：

- `media_core.code-workspace`

这个子项目已经补好了以下索引相关配置：

- `build/compile_commands.json`
- `.clangd`
- `.vscode/settings.json`

首次单独打开时，建议等待几秒让 CMake/clangd 完成索引，再尝试跳转 `IDemuxer`、`Status` 等符号。

## 实施步骤（分阶段）

### Phase 1：最小可运行

- 完成文件视频转码闭环
- 验收：可从 mp4 输入转码输出 mp4，尾帧不丢（含 flush）

### Phase 2：硬解接入

- 增加硬件探测、设备初始化与软解回退
- 验收：可打印探测结果；硬解失败时自动切到软解并完成转码

### Phase 3：WebRTC 适配

- 解耦输入输出端，接入 WebRTC 编码输出适配层
- 验收：同一 encoder 逻辑可被“文件转码”和“WebRTC 发送”两端复用
