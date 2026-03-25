# Transcoder (C++ libav*)

一个教学友好的 FFmpeg `libav*` 转码器实现：
- 输入任意常见媒体文件（如 MKV）
- 输出 MP4
- 默认只保留第一路视频 + 第一路音频，忽略字幕
- 默认重编码（视频 `H.264 CRF/Preset` + 音频 `AAC`）
- 支持可选 `--copy` 直拷贝模式（不重编码）

> 设计文档见 `DESIGN.md`。本 README 重点讲“如何构建、运行、验证”。

## 1. 项目结构

```text
project/transcoder/
├── CMakeLists.txt
├── DESIGN.md
├── README.md
├── src/
│   └── main.cpp
└── scripts/
    ├── make_sample.sh
    └── validate_output.sh
```

### 1.1 文件职责说明

- `CMakeLists.txt`：定义构建流程，查找 `libav*` 依赖并生成 `transcoder` 可执行文件。
- `DESIGN.md`：阶段1架构与原理设计文档，解释为什么这样做、时间戳如何处理、如何验收。
- `README.md`：使用说明文档，包含环境准备、构建方式、运行示例和问题排查。
- `src/main.cpp`：核心业务代码，负责参数解析、流选择、转码/拷贝、时间戳换算、进度与错误码。
- `scripts/make_sample.sh`：生成本地测试样例媒体（用于快速回归验证）。
- `scripts/validate_output.sh`：用 `ffprobe` 自动验收输出文件（容器、流类型、音画时长差等）。

### 1.2 运行产物（执行后出现）

- `build/`：CMake 构建目录，包含目标文件和最终可执行文件 `build/transcoder`。
- `sample_input.mkv`：测试输入样例（可通过脚本生成）。
- `sample_output*.mp4`：测试输出样例（转码结果文件）。

## 2. 实现路线（为什么这么做）

- 使用标准流水线：`Demux -> Decode -> Encode -> Mux`
- `--copy` 模式走 `Demux -> Mux`（remux）
- 时间戳策略：
  - 解码后帧时间戳从输入 `time_base` 转换到编码器 `time_base`
  - 封装前 packet 再转换到输出流 `time_base`
  - 关键 API：`av_rescale_q` / `av_packet_rescale_ts`

这样做的好处是：原理清晰，和 FFmpeg 官方样例思路一致，适合初学者定位问题。

## 3. 环境依赖

需要安装：
- C++17 编译器（clang 或 gcc）
- CMake (>= 3.16)
- pkg-config
- FFmpeg 开发库：
  - `libavformat`
  - `libavcodec`
  - `libavutil`
  - `libswresample`
  - `libswscale`

### macOS（Homebrew 示例）

```bash
brew install ffmpeg cmake pkg-config
```

### Ubuntu/Debian 示例

```bash
sudo apt update
sudo apt install -y \
  ffmpeg \
  libavformat-dev libavcodec-dev libavutil-dev \
  libswresample-dev libswscale-dev \
  cmake pkg-config g++
```

## 4. 构建

```bash
cd project/transcoder
cmake -S . -B build
cmake --build build -j
```

可执行文件：
- `build/transcoder`

## 5. 运行示例

### 5.1 默认重编码（推荐）

```bash
./build/transcoder \
  --input input.mkv \
  --output output.mp4 \
  --crf 23 \
  --preset medium \
  --audio-bitrate 128k
```

### 5.2 指定流索引（只保留一条视频和一条音频）

```bash
./build/transcoder \
  --input input.mkv \
  --output output.mp4 \
  --video-index 0 \
  --audio-index 1
```

### 5.3 直拷贝模式（无重编码）

```bash
./build/transcoder \
  --input input.mkv \
  --output output.mp4 \
  --copy
```

> 注意：`--copy` 只做重封装，若输入编码与 MP4 不兼容，可能失败。

### 5.4 按语言标签选音轨

```bash
./build/transcoder \
  --input input.mkv \
  --output output.mp4 \
  --audio-lang eng
```

## 6. 参数说明

- `--input`：输入文件路径（必填）
- `--output`：输出 MP4 路径（必填）
- `--crf`：视频质量，越小越清晰、文件更大（默认 `23`）
- `--preset`：编码速度/压缩率平衡（默认 `medium`）
- `--audio-bitrate`：音频码率（默认 `128k`）
- `--video-index`：指定输入视频流索引
- `--audio-index`：指定输入音频流索引
- `--audio-lang`：按语言标签选音轨（如 `eng`、`chi`），若未命中会回退到第一路音频
- `--copy`：启用直拷贝模式

## 7. 最小可验证测试

### 7.1 生成样例输入（包含额外流，便于测试流选择）

```bash
chmod +x scripts/make_sample.sh scripts/validate_output.sh
./scripts/make_sample.sh sample_input.mkv
```

### 7.2 执行转码

```bash
./build/transcoder --input sample_input.mkv --output sample_output.mp4 --crf 23 --preset medium --audio-bitrate 128k
```

### 7.3 自动验收（ffprobe）

```bash
./scripts/validate_output.sh sample_input.mkv sample_output.mp4 0.08
```

验收脚本会检查：
- 输出是否是 MP4
- 输出是否最多 1 路视频 + 1 路音频，且无字幕
- 音视频时长差是否小于阈值（默认 80ms）
- 输出文件是否非空

## 8. 日志、进度与返回码

- 日志分级：`[INFO]` / `[WARN]` / `[ERROR]`
- 转码时会按输入时间戳打印百分比进度（粗粒度）
- 关键返回码：
  - `101` 参数错误
  - `201-204` 输入/探测/流选择/输出初始化错误
  - `301` 解码错误
  - `401` 编码错误
  - `501` 封装错误
  - `601` 运行时处理错误
## 9. 常见问题排查

- 链接失败找不到 `libav*`
  - 检查是否安装了开发包（不是仅运行时 ffmpeg）
  - 检查 `pkg-config --modversion libavformat` 是否可用
- `--copy` 模式失败
  - 输入编码可能不适合 MP4，改用默认重编码模式
- 音画不同步
  - 优先用默认重编码模式并检查 `validate_output.sh`
  - 若是极端 VFR 源，先用 ffprobe 查看输入时间戳是否异常

## 10. 下一步可扩展项

- 多音轨/按语言选轨
- 字幕保留与转封装
- 硬件编码（VideoToolbox/NVENC）
- 进度显示与更细日志
