# Day 5：MP4 转 TS 转码实战

## 1. 本节目标

- 学会用 FFmpeg 把 MP4 转成 TS。
- 理解两种模式：**封装转换（不重编码）** 与 **重新编码（重编码）**。
- 学会用 `ffprobe` 验证输出结果是否符合预期。

---

## 2. 先明确：你要的是“转封装”还是“转码”

很多同学会把“MP4 转 TS”都叫转码，但实际上有两种不同操作：

1. **封装转换（Remux）**  
   音视频编码不变，只更换容器：MP4 容器 → TS 容器。  
   优点：快、无额外画质损失。  
   前提：输入编码要被 TS 容器和目标播放链路支持。

2. **重新编码（Transcode）**  
   对视频/音频重新编码后再放入 TS。  
   优点：可统一编码参数（码率、GOP、帧率等），更适合直播/分发规范。  
   代价：耗时增加，可能有画质损失。

---

## 3. 命令实战

### 3.1 场景 A：尽量不重编码（首选）

```bash
ffmpeg -i input.mp4 -map 0 -c copy -bsf:v h264_mp4toannexb -f mpegts output.ts
```

**参数讲解：**

- `-map 0`：映射输入中的全部流（视频、音频、字幕等）。  
- `-c copy`：不重编码，直接复制编码数据。  
- `-bsf:v h264_mp4toannexb`：把 MP4 中常见 H.264 格式转换为 TS 友好的 Annex B。  
- `-f mpegts`：明确指定输出封装为 TS。

**适用：**

- 你只想快速转换容器；
- 输入视频本身是 H.264，音频是 AAC/MP2/AC3 等常见可用格式。

---

### 3.2 场景 B：视频重编码为 H.264，音频转 AAC

```bash
ffmpeg -i input.mp4 \
  -map 0:v:0 -map 0:a:0 \
  -c:v libx264 -preset medium -crf 23 -g 50 -keyint_min 50 \
  -c:a aac -b:a 128k -ac 2 \
  -f mpegts output_reencode.ts
```

**参数讲解：**

- `-c:v libx264`：视频统一转 H.264，兼容性高。  
- `-crf 23`：画质/体积平衡值，越小画质越高、码率越大。  
- `-g 50 -keyint_min 50`：关键帧间隔约 2 秒（假设 25fps），便于切片与随机访问。  
- `-c:a aac -b:a 128k`：音频转 AAC 128k。  

**适用：**

- 输入编码复杂或不统一；
- 你要产出规范化 TS 用于分发、切片或直播链路。

---

### 3.3 场景 C：输出分段 TS（为 HLS 做准备）

```bash
ffmpeg -i input.mp4 \
  -c:v libx264 -crf 23 -g 50 -keyint_min 50 \
  -c:a aac -b:a 128k \
  -f segment -segment_time 6 -segment_format mpegts \
  "seg_%03d.ts"
```

**参数讲解：**

- `-f segment`：使用分段 muxer。  
- `-segment_time 6`：每段约 6 秒。  
- `-segment_format mpegts`：每段输出为 TS。

**适用：**

- 你需要把文件拆成多个 TS 小片段（如后续接 HLS 流程）。

---

## 4. 如何验证转换结果

### 4.1 看封装与编码信息

```bash
ffprobe -hide_banner -show_streams output.ts
```

重点关注：

- `codec_name`（确认视频/音频编码是否符合预期）
- `codec_type`（是否有视频流、音频流）
- `time_base`、`start_time`、`duration`（时基与时长是否正常）

### 4.2 快速检查 TS 包级正确性

- 用你在作业 2 写的 TS 解析器检查：
  - 是否按 188 字节对齐；
  - 同步字节是否为 `0x47`；
  - PID 是否稳定存在（视频/音频 PID）。

---

## 5. 常见问题与排查

1. **输出文件能生成但播放器黑屏**  
   可能是编码不兼容或参数不合理。先尝试场景 B（重编码 H.264 + AAC）。

2. **报“non monotonically increasing dts”**  
   可尝试增加时间戳处理参数，例如：
   `-fflags +genpts` 或检查输入是否有损坏时间戳。

3. **`-c copy` 后失败**  
   输入流可能不适合直接塞入 TS。改为指定 `-c:v libx264 -c:a aac` 重编码。

4. **体积变大很多**  
   重编码参数过于保守（比如 CRF 太低或码率太高），可适当调大 `-crf`（如 23→25）。

---

## 6. 一页总结

- **想快、无损优先**：先试 `-c copy` + `-bsf:v h264_mp4toannexb`。  
- **想稳、规范统一**：用 `libx264 + aac` 重编码。  
- **想分发切片**：用 `segment` 输出多个 `.ts`。  
- 每次转换后都用 `ffprobe` 验证，避免“命令成功但结果不可用”。

下一步：完成 `assignments/assignment_4.md`，并把命令输出与参数解释填写进去。
