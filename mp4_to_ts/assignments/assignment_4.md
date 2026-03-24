# 作业 4：MP4 转 TS 转码实战

## 目标

通过 FFmpeg 完成 MP4 → TS 的两种方式（封装转换与重编码），并能解释关键参数与输出差异。

---

## 要求

1. **方式 A：封装转换（不重编码）**

至少执行一次类似命令：

```bash
ffmpeg -i input.mp4 -map 0 -c copy -bsf:v h264_mp4toannexb -f mpegts output_copy.ts
```

2. **方式 B：重编码输出 TS**

至少执行一次类似命令：

```bash
ffmpeg -i input.mp4 \
  -map 0:v:0 -map 0:a:0 \
  -c:v libx264 -crf 23 -g 50 -keyint_min 50 \
  -c:a aac -b:a 128k \
  -f mpegts output_reencode.ts
```

3. **结果验证**

对两个输出文件分别执行：

```bash
ffprobe -hide_banner -show_streams output_copy.ts
ffprobe -hide_banner -show_streams output_reencode.ts
```

并回答：

- 两个输出的 `codec_name` 是否一致？为什么？
- 文件大小与处理耗时有什么差异？原因是什么？
- 你在什么场景下会选方式 A，什么场景下会选方式 B？

---

## 作答区

### 1. 输入文件信息

- 输入文件路径：
- 输入文件编码（可贴 ffprobe 关键字段）：

### 2. 方式 A 命令与输出

- 命令：
- 关键日志（可节选）：
- 输出文件大小：
- ffprobe 结果要点：

### 3. 方式 B 命令与输出

- 命令：
- 关键日志（可节选）：
- 输出文件大小：
- ffprobe 结果要点：

### 4. 对比结论

- 编码差异：
- 体积/耗时差异：
- 场景选择建议：

---

## 验证标准

- 至少成功生成 2 个 TS 文件（copy 与 reencode 各一个）。
- 能用 `ffprobe` 证明输出流信息，并做有依据的对比。
- 能清晰解释核心参数：`-c copy`、`-c:v libx264`、`-crf`、`-g`、`-f mpegts`、`-bsf:v h264_mp4toannexb`。

---

## 参考答案（含讲解）

> 以下为示例答案，你可按自己机器上的文件路径和输出结果替换。

### 1. 输入文件信息（示例）

- 输入：`sample.mp4`
- 视频编码：`h264`
- 音频编码：`aac`

### 2. 方式 A（copy）示例

命令：

```bash
ffmpeg -i sample.mp4 -map 0 -c copy -bsf:v h264_mp4toannexb -f mpegts sample_copy.ts
```

示例现象：

- 处理速度通常很快（接近实时或远快于实时）。
- 输出编码一般仍是 `h264 + aac`（因为没有重编码）。
- 文件体积通常与输入接近（封装开销有少量变化）。

讲解：

- `-c copy` 不会改编码，只改容器，所以速度最快且无新增画质损失。
- `h264_mp4toannexb` 负责把 H.264 比特流调整为 TS 常见格式要求。

### 3. 方式 B（reencode）示例

命令：

```bash
ffmpeg -i sample.mp4 \
  -map 0:v:0 -map 0:a:0 \
  -c:v libx264 -crf 23 -g 50 -keyint_min 50 \
  -c:a aac -b:a 128k \
  -f mpegts sample_reencode.ts
```

示例现象：

- 处理速度明显慢于 copy（因为要重新编码）。
- 视频编码由 `libx264` 重新生成，码率与质量由 CRF 决定。
- 文件体积可能变大或变小，取决于输入复杂度与 CRF/码率设定。

讲解：

- 重编码适合“统一输出规范”，比如直播前处理、分发前压制。
- `-g 50`（假设 25fps）约每 2 秒一个关键帧，有利于分段播放和切片稳定性。

### 4. 对比结论（示例）

- **编码差异：**  
  copy 方案通常保持原编码；reencode 方案会按照你指定编码器重新生成流。

- **体积/耗时差异：**  
  copy 更快、CPU 占用低；reencode 更慢但可控性更高（码率、GOP、画质都可调）。

- **场景选择：**  
  - 只做快速容器转换、且输入编码兼容目标链路：优先 copy。  
  - 需要统一编码参数、提升兼容性或满足分发规范：选择 reencode。
