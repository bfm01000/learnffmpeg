# 阶段零：C++ 音视频基础 self-check 清单

> 在进入 WebRTC 项目设计之前，先把音视频基础查一遍。
> 共 **40 题**，按 7 大类组织。每题包含：**面试官的高频提问** → **100-200 字标准答案** → **自检 Y/N**。
> 答案是"能口述出来"的版本，不是"看得懂"的版本——读完一题，闭上文档自己讲一遍，讲不通就标 N。
>
> **建议投入**：3-5 天。N 项超过 10 个的话，去针对性看资料/复习；N 项少于 5 个，可以直接进阶段二。

---

## 目录

- [一、像素与采样格式（5 题）](#一像素与采样格式5-题)
- [二、编解码码流结构（8 题）](#二编解码码流结构8-题)
- [三、容器与封装（5 题）](#三容器与封装5-题)
- [四、时间戳与音画同步（6 题）](#四时间戳与音画同步6-题)
- [五、FFmpeg 核心 API（6 题）](#五ffmpeg-核心-api6-题)
- [六、直播协议对比（5 题）](#六直播协议对比5-题)
- [七、音频处理基础（5 题）](#七音频处理基础5-题)
- [八、知识地图（已覆盖 vs 空白）](#八知识地图已覆盖-vs-空白)

---

## 一、像素与采样格式（5 题）

### Q1. YUV420P 的内存布局是怎样的？为什么主流编码都用 4:2:0？

**面试官提问**："你说一下 YUV420P 在内存里怎么排？为什么不用 YUV444？"

**标准答案**：YUV420P / I420 是平面式（planar）布局，三个分量分开存：先放完整的 Y 平面，再放 1/4 大小的 U 平面，最后放 1/4 大小的 V 平面。以 `4×4` 图像为例，内存不是按像素交错存 `YUVYUV...`，而是这样连续排：

```text
Y 平面：width × height = 4 × 4
data[0] -> Y00 Y01 Y02 Y03
           Y10 Y11 Y12 Y13
           Y20 Y21 Y22 Y23
           Y30 Y31 Y32 Y33

U 平面：(width/2) × (height/2) = 2 × 2
data[1] -> U00 U01
           U10 U11

V 平面：(width/2) × (height/2) = 2 × 2
data[2] -> V00 V01
           V10 V11
```

在 FFmpeg 的 `AVFrame` 里，`data[0]` 指向 Y 首地址，`data[1]` 指向 U 首地址，`data[2]` 指向 V 首地址；每一行移动时不要用 `width` 硬跳，要用 `linesize[0] / linesize[1] / linesize[2]`，因为真实 stride 可能因 16/32/64 字节对齐大于逻辑宽度。"4:2:0" 表示每个 `2×2` 的 Y 像素共享一组 U/V，也就是色度水平、垂直方向都减半采样。主流编码默认 4:2:0，是因为人眼对亮度敏感、对色度不敏感，能把 YUV444 的 `3.0` 字节/像素降到 `1.5` 字节/像素，压缩效率收益很大。

**自检**：你能口述清楚吗？[Y]  
**已有文档**：`Doc/ffmpeg/pixel_format_memory_layout_guide.md`

---

### Q2. NV12 / NV21 和 YUV420P 的区别？为什么硬件编解码器爱用 NV12？

**面试官提问**："摄像头/硬解出来通常是 NV12 不是 I420，区别在哪？"

**标准答案**：三者都是 4:2:0，区别只在色度平面怎么排。**YUV420P / I420** 是三平面，Y、U、V 各自连续；**NV12** 是双平面，Y 单独一层，U/V 在同一个平面里按 `UVUV...` 交错；**NV21** 也是双平面，但顺序反过来是 `VUVU...`。

```text
I420 / YUV420P：
data[0] -> YYYYYYYYYYYYYYYY     4×4 的 Y
data[1] -> UUUU                 2×2 的 U
data[2] -> VVVV                 2×2 的 V

NV12：
data[0] -> YYYYYYYYYYYYYYYY     4×4 的 Y
data[1] -> U0 V0 U1 V1          2×2 的 UV 交错
           U2 V2 U3 V3

NV21：
data[0] -> YYYYYYYYYYYYYYYY     4×4 的 Y
data[1] -> V0 U0 V1 U1          2×2 的 VU 交错
           V2 U2 V3 U3
```

对应到 `AVFrame`，I420 会用到 `data[0] / data[1] / data[2]` 三个指针；NV12/NV21 通常只用 `data[0]` 指向 Y、`data[1]` 指向交错的 UV/VU，`data[2]` 不再代表独立 V 平面。访问第 `(x, y)` 个亮度像素看 `data[0] + y * linesize[0] + x`；访问色度时要先把坐标缩半成 `(x/2, y/2)`，NV12 的 U/V 地址是 `data[1] + (y/2) * linesize[1] + (x/2) * 2` 和后一个字节，NV21 则 V/U 顺序相反。硬件编解码器偏爱 NV12，是因为双平面对 DMA、SIMD 和 GPU 纹理采样都更友好，UV 一次读取就是一组色度。

**自检**：你能口述清楚吗？[N]  
**已有文档**：`Doc/ffmpeg/pixel_format_memory_layout_guide.md`

---

### Q3. PCM 音频的采样率/位深/通道怎么算字节数？立体声 44.1kHz 16bit 一秒多少 KB？

**面试官提问**："立体声 44.1kHz、16 位采样，一秒原始 PCM 多大？1 帧 1024 采样的 AAC 编码前是多少字节？"

**标准答案**：PCM 字节数 = `采样率 × 位深/8 × 通道数 × 时长(秒)`。立体声 44.1kHz 16bit 一秒 = `44100 × 2 × 2 = 176400 字节 ≈ 172 KB`。一帧 1024 采样的 AAC 对应原始 PCM = `1024 × 2 × 2 = 4096 字节`。**坑点**：① 立体声 PCM 默认是"交错格式"（LRLRLR...），FFmpeg 的 `AV_SAMPLE_FMT_S16` 是交错的、`AV_SAMPLE_FMT_S16P` 是平面的；② 位深 16bit/24bit/32bit 对应不同的 SampleFormat 枚举；③ AAC/Opus 等编码器通常要求**平面式**输入，所以拿到摄像头/麦克风的交错 PCM 要先用 `swr_convert` 转一次。

**自检**：你能口述清楚吗？[ N]  
**已有文档**：`Doc/ffmpeg/avsampleformat_interview_guide.md`

---

### Q4. 什么是 stride（行跨度）？为什么 width 和 stride 经常不相等？

**面试官提问**："你解码出来一帧 YUV，stride 比 width 大，多出来的那部分是什么？直接按 width 拷贝会怎样？"

**标准答案**：**stride（也叫 linesize / pitch）** 是图像一行在内存里实际占用的字节数，包含每行末尾的对齐填充字节；**width** 是逻辑像素宽度。两者经常不相等是因为硬件/SIMD 要求每行起始地址按 16/32/64 字节对齐（FFmpeg 默认 32），对齐后剩余空间用 padding 填充。例如 width=1080 的 Y 平面，stride 可能是 1088 或 1120。**直接按 width 拷贝会导致图像撕裂/绿屏**——必须用双层循环逐行拷贝 width 字节、按 stride 步进指针。FFmpeg 的 `av_image_copy` 内部就是这么做的。**面试常追问**：怎么把带 stride 的 AVFrame 转成紧凑（width=stride）的 buffer？用 `av_image_copy_to_buffer`。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/pixel_format_memory_layout_guide.md`

---

### Q5. RGB 和 YUV 互转为什么不是无损的？

**面试官提问**："我把 RGB888 转 YUV420 再转回 RGB，颜色为什么变了？"

**标准答案**：两个损失来源：① **色度子采样**：YUV420 把 4 个像素的 UV 平均成一组，转回 RGB 时每个像素的色度都用这组共享值，相邻像素细节丢失（最明显在锐利的红绿过渡边缘）；② **浮点 → 整数取整**：RGB↔YUV 的转换矩阵（BT.601 / BT.709 / BT.2020）含浮点系数，转 YUV 时取整截断、转回时再次取整，往复有累积误差。**面试坑**：转换矩阵要选对——SDR 视频用 BT.709，老式 SD 视频用 BT.601，HDR 用 BT.2020；选错会导致整体偏色（绿/紫调）。FFmpeg 里 `sws_setColorspaceDetails` 控制矩阵；OpenGL/Metal shader 要自己写矩阵常量。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/swscontext_lecture.md`

---

## 二、编解码码流结构（8 题）

### Q6. H264 的 NALU 是什么？类型字段在哪？怎么区分 IDR / 普通 P 帧 / SPS / PPS？

**面试官提问**："拿到一段 H264 裸流，你怎么解析出第一个 IDR 帧的位置？"

**标准答案**：NALU（Network Abstraction Layer Unit）是 H264 的传输单元，每个 NALU = `NALU Header (1 字节) + RBSP 数据`。Header 的低 5 位是 `nal_unit_type`：1 = 非 IDR 的 P/B 帧，5 = IDR 帧，7 = SPS，8 = PPS，6 = SEI。**Annex-B 格式**用起始码 `0x000001` 或 `0x00000001` 分隔 NALU；**AVCC 格式**用 4 字节长度前缀 + NALU 数据（MP4 容器内默认 AVCC）。解析裸流时，扫描起始码切分 NALU，读 Header 第一个字节 `& 0x1F` 取类型，遇到 type=5 即 IDR 关键帧。**面试常追问**：emulation prevention 字节（0x03）是干什么的？防止 RBSP 数据里出现 0x000001 起始码模式被误识别。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/h264_mp4_面试速记.md`、`Doc/ffmpeg/h264_mp4_模拟面试.md`

---

### Q7. SPS / PPS 是干什么的？为什么 IDR 之前必须有它们？

**面试官提问**："如果丢了 SPS，解码器能解 IDR 吗？SPS 一般多大？"

**标准答案**：**SPS（Sequence Parameter Set）** 描述整个视频序列的属性：分辨率、profile/level、色度采样、参考帧数量、是否隔行扫描等；**PPS（Picture Parameter Set）** 描述图像级参数：熵编码模式（CABAC/CAVLC）、量化参数初值、是否使用加权预测等。解码器初始化必须先吃到 SPS+PPS，才知道怎么开辟解码缓冲、怎么解析后续 slice。丢了 SPS 解码器直接报错。一个 SPS 通常 10-30 字节，PPS 4-10 字节。**面试坑**：① RTMP/FLV 是把 SPS+PPS 放在 AVCDecoderConfigurationRecord 里（一次性发），MPEG-TS 是周期性内联在码流里（容错切换流）；② **每个 IDR 之前要不要重发 SPS/PPS** 取决于场景——直播场景必须重发（观众可能中途加入），点播只要文件头有就够。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/h264_mp4_模拟面试.md`

---

### Q8. Annex-B 和 AVCC 的区别？什么场景用哪个？

**面试官提问**："你从 RTMP 拿到的 H264 数据为什么直接喂解码器解不出来？"

**标准答案**：**Annex-B** 用起始码 `0x00 00 00 01`（或 3 字节版本）分隔 NALU，连续拼接即可，适合**字节流场景**（MPEG-TS、RTSP raw、文件直存）；**AVCC**（也叫 length-prefix）用 4 字节大端长度前缀 + NALU 内容，并要求把 SPS/PPS 独立放在 extradata 里，适合**封装容器场景**（MP4、FLV/RTMP）。两者**不能互换**直接喂解码器——MP4 里的 AVCC 必须先转成 Annex-B 才能给 FFmpeg 软解（除非显式设置 `bsf=h264_mp4toannexb`），否则解码器把 4 字节长度前缀当成起始码扫描失败。**FFmpeg 里**：`bsf=h264_mp4toannexb` 做 AVCC→Annex-B，`bsf=h264_metadata` 反向操作。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/h264_mp4_模拟面试.md`、`Doc/ffmpeg/mp4-2-ts.md`

---

### Q9. H264 / H265 / VP9 / AV1 的核心区别？面试怎么对比？

**面试官提问**："为什么你的项目选 H264 不选 H265？说一下编码效率对比。"

**标准答案**：**压缩效率（同画质下码率）**：AV1 ≈ H265 × 0.7 ≈ VP9 × 0.75 ≈ H264 × 0.5。**编解码复杂度（CPU 占用）**：AV1 > H265 > VP9 > H264，AV1 软编是 H264 的 5-10 倍。**专利成本**：H264/H265 要付 MPEG-LA 专利费（H265 因专利混乱很多公司转 AV1），VP9/AV1 免费。**硬件覆盖率**：H264 几乎所有设备硬解硬编，H265 中高端设备覆盖，VP9 Android 较好/iOS 差，AV1 仅最新旗舰。**WebRTC 实际选型**：默认 VP8/H264（兼容性优先），高级场景可协商 VP9/H265/AV1。**面试一句话答**："实时通信我会选 H264——硬件覆盖好、延迟低、CPU 友好；离线场景可以上 H265/AV1 换码率收益。"

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/码率-Profile.md`（部分）

---

### Q10. I/P/B 帧的区别？B 帧为什么会增加延迟？

**面试官提问**："实时通信为什么不开 B 帧？B 帧引入多少延迟？"

**标准答案**：**I 帧（Intra）** 帧内编码，不依赖其他帧，体积最大（关键帧/IDR 帧是特殊的 I 帧，能作为解码起点）；**P 帧** 前向预测，依赖前一个 I/P 帧，体积约为 I 帧的 1/3；**B 帧** 双向预测，依赖前后参考帧，体积最小（约 I 帧的 1/10）。B 帧的延迟来自**编解码乱序**：编码顺序 IBBP 实际解码顺序是 IPBB（因为 B 必须等到后面的 P 解出来才能解），解码器必须先缓冲 P，再回头解 B，再按显示顺序输出——一个 B 帧约引入 1 帧（33ms@30fps）的延迟，连续 2 个 B 帧约 66ms。**RTC 实时场景必须关 B 帧**（`x264 --bframes 0`），换延迟降低；**点播/直播可以用 2-3 个 B 帧**换码率收益。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（基础知识，散见于多处）

---

### Q11. AAC 的 ADTS 和 ADIF 区别？ADTS 头是怎样的？

**面试官提问**："你从 FLV 里拿到 AAC 数据，直接保存能播放吗？为什么？"

**标准答案**：**ADTS（Audio Data Transport Stream）** 是 AAC 的流式封装，每帧前加 7 或 9 字节同步头（含 sync word `0xFFF`、profile、采样率索引、通道数、帧长度），适合**流式播放、随机切入**；**ADIF（Audio Data Interchange Format）** 是文件封装，只在开头有一个头，适合存档但不能流式切入。**FLV/MP4 容器里的 AAC 是 raw AAC**（没有 ADTS 头），SamplingRate 和 ChannelConfig 放在 `AudioSpecificConfig`（FLV 的 AAC sequence header / MP4 的 esds box）。直接保存 raw AAC 播放器不认——必须**给每帧补 ADTS 头**才能存成 `.aac` 文件播放。**面试坑**：ADTS 头里 `frame_length` 包含头本身，profile 字段值要 `-1`（ADTS 里 1=MAIN，AudioSpecificConfig 里 2=AAC-LC）。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（空白，建议补）

---

### Q12. Opus 编码器是什么？为什么 WebRTC 默认用它？

**面试官提问**："WebRTC 音频默认 Opus 还是 AAC？为什么？"

**标准答案**：Opus 是 IETF 标准化的现代音频编码器，**WebRTC 默认必选**（RFC 7587 强制）。核心优势：① **双模融合**——低码率（< 32kbps）用 SILK 模式（适合语音），高码率（> 32kbps）用 CELT 模式（适合音乐），中间段混合；② **超宽频率范围**：6 kbps 到 510 kbps 全覆盖；③ **低延迟**：可配置 2.5-60ms 帧长，5ms 帧时端到端延迟仅 22ms（AAC-LC 最低也要 ~60ms）；④ **强抗丢包**：内置 FEC（前向纠错）和 PLC（丢包隐藏）；⑤ **免费免专利**。对比 AAC：AAC 在中高码率（128k+）音乐质量更好、生态成熟，但延迟高、丢包恢复差，不适合实时通信。**一句话答**：Opus = 实时通信的事实标准，AAC = 直播/点播音乐场景的事实标准。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（空白，建议补）

---

### Q13. 量化参数 QP / CRF / 码率控制模式（CBR/VBR/ABR）的区别？

**面试官提问**："直播推流你用 CBR 还是 CRF？为什么？"

**标准答案**：**QP（量化参数）** 是编码器内部的量化档位，0-51 范围（H264），值越大压缩越狠画质越差，每帧 QP 不同。**CRF（Constant Rate Factor）** 是 x264 的智能模式，目标恒定主观画质——平移镜头给低 QP，静止镜头给高 QP，总体码率浮动但画质稳定；**适合点播离线压制**，不适合直播（瞬时码率不可预测，会冲爆带宽）。**CBR（恒定码率）** 严格保持目标码率（用填充 NALU 补齐），适合**直播推流**（带宽可预测、CDN 友好），代价是复杂场景画质骤降。**VBR（可变码率）** 画质优先、码率可上下浮动，适合点播。**ABR（平均码率）** 长期均值匹配目标但短期浮动，介于 CBR 和 VBR 之间。**直播推流标配 CBR**，目标码率根据分辨率定（720p 1.5-2Mbps，1080p 3-4Mbps）。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/码率-Profile.md`

---

## 三、容器与封装（5 题）

### Q14. FLV / MP4 / TS / WebM / MKV 的核心区别？

**面试官提问**："为什么 RTMP 用 FLV 不用 MP4？为什么 HLS 切片用 TS 不用 MP4？"

**标准答案**：核心维度是**"能否流式切入"和"索引位置"**。**FLV** 极简结构（Tag 流），无全局索引、可流式追加、单个 Tag 可独立解码——适合 RTMP 直播推流。**MP4** Box 嵌套结构，moov box 含全局索引（采样偏移/时长），**moov 在文件末尾时无法流式播放**（必须下完整个文件才能解析），所以做"渐进式播放"必须用 `faststart` 把 moov 移到前面（或用 fMP4 把 moov 切碎插在数据前）。**TS（MPEG-TS）** 188 字节固定包大小，每个 TS 包都自带 PCR/PTS，**任意切入都能解码**——HLS 切片首选。**WebM** 基于 Matroska，只装 VP8/VP9/Opus/Vorbis，Web 端友好。**MKV** 万能容器，支持任意编码 + 多音轨字幕。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/mp4-2-ts.md`

---

### Q15. MP4 的 moov box 是什么？faststart 优化是干什么？

**面试官提问**："为什么你下载到一半的 MP4 用播放器打不开，但下载到一半的 FLV 可以播？"

**标准答案**：MP4 的 **moov box** 存所有采样的偏移、时长、关键帧索引——是播放器的"目录"。FFmpeg 默认把 moov 写在文件末尾（因为编码时不知道总时长/总采样数，写完数据回头再写 moov 最简单）。播放器拿到没有 moov 的 MP4 完全不知道在哪解码，所以下到一半放不了。**faststart** 优化把 moov 移到 mdat（数据）之前：FFmpeg 加参数 `-movflags +faststart`，原理是编码完后做一次"两遍写"——先按常规写、再把 moov 搬到前面。代价是输出比常规慢一点点。**面试常追问**：fMP4（fragmented MP4）怎么解决这个问题？把 moov 拆成多个 moof，每个 fragment 自带索引，DASH/CMAF 流媒体的基础。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/mp4_h264_mock_interview.md`

---

### Q16. MPEG-TS 为什么是 188 字节？PCR 是什么？

**面试官提问**："TS 包为什么固定 188 字节？这个数有什么来历？"

**标准答案**：188 字节 = 4 字节包头 + 184 字节净荷，历史原因是**为了对齐 ATM 信元（53 字节）**——4 个 ATM 信元（212 字节）减去 ATM 头（24 字节）正好 188，便于电信网络传输。包头含 13 位 PID（标识所属流，PMT/PAT 通过 PID 关联）、4 位连续计数（检测丢包）、自适应字段（可携带 PCR）。**PCR（Program Clock Reference）** 是节目时钟参考，**给解码器同步用的真实时钟**（不是 PTS），周期性插入码流（通常 < 100ms 一次）。解码器用 PCR 校准自己的 STC（系统时钟），再用 PTS 决定何时显示。HLS 切片要求每个 .ts 文件自包含 PCR/PMT/PAT，所以任意切入都能播。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/mp4-2-ts.md`

---

### Q17. 什么是封装格式的"自包含"和"非自包含"？为什么 HLS 切片要自包含？

**面试官提问**："你切 HLS 的时候第二个分片为什么也要带 SPS/PPS/PAT/PMT？"

**标准答案**：**自包含**指单个文件/分片含全部解码必需的信息（SPS/PPS、PAT/PMT、音频配置），不依赖前置文件即可独立解码。**非自包含**反之（例如普通 MP4 的非首个 fragment）。HLS 是 HTTP 短连接拉流，**观众可能从任意分片开始播放**（点击进度条跳转、CDN 切换源、网络中断重连），如果第二个分片缺 SPS/PPS，新接入的观众永远解不出图像。所以 HLS 切片必须**每个 .ts 自带 PAT/PMT、且第一帧是 IDR + 前置 SPS/PPS**。FFmpeg 用 `-hls_flags +independent_segments` + `-force_key_frames` 保证。CMAF/fMP4 通过 `init segment + media segments` 解决：init 段单独下载、所有 media 段共用，少传 SPS/PPS 但要保证每个 media 段开头有 IDR。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（部分覆盖于 mp4-2-ts.md）

---

### Q18. FLV 的 Tag 结构是怎样的？Script Tag / Audio Tag / Video Tag 各干什么？

**面试官提问**："你解析 FLV 文件，AAC sequence header 在哪个 Tag 里？"

**标准答案**：FLV 文件 = 13 字节头（"FLV"+ 版本+ flags + 头长）+ 连续的 Tag 流。每个 Tag = 11 字节 Tag 头（类型 1 字节、数据长度 3 字节、时间戳 3+1 字节、StreamID 3 字节）+ 数据 + 4 字节 PreviousTagSize。**三种 Tag**：① **Script Tag**（类型 18）：onMetaData，第一个 Tag，含视频时长、宽高、码率等元数据；② **Audio Tag**（类型 8）：第一字节是 SoundFormat 等，后续是音频数据。**AAC 的第一个 Audio Tag 是 AAC sequence header**（含 AudioSpecificConfig），后续才是 AAC raw 数据；③ **Video Tag**（类型 9）：第一字节是帧类型 + CodecID，第二字节是 AVCPacketType。**H264 的第一个 Video Tag 是 AVC sequence header**（含 SPS/PPS），后续是 AVCC 格式的 NALU。FLV 时间戳是 ms 级、24+8 位（约支持 4.6 天）。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（空白，建议补）

---

## 四、时间戳与音画同步（6 题）

### Q19. PTS 和 DTS 的区别？没有 B 帧时它们一定相等吗？

**面试官提问**："你解封装拿到的 packet 里 PTS 和 DTS 不一样，怎么解释？"

**标准答案**：**DTS（Decoding Time Stamp）** 是解码顺序时间戳，告诉解码器"什么时候开始解这个包"；**PTS（Presentation Time Stamp）** 是显示顺序时间戳，告诉渲染器"什么时候把这个帧显示出来"。**无 B 帧时**，编码顺序 = 显示顺序，所以每个包的 PTS 必然等于 DTS。**有 B 帧时**，编码顺序 IBBP，解码必须按 IPBB（B 依赖后面的 P），所以一个 B 帧的 DTS < PTS（早解码晚显示）。**FFmpeg 里**：解封装拿到 AVPacket 时有 pts/dts，解码后 AVFrame 只有 pts（DTS 没意义了）。**面试坑**：① pts/dts 的单位是 **time_base**（不是毫秒），换算 `pts_in_seconds = pts × av_q2d(time_base)`；② MP4 容器的 time_base 是 1/timescale（常见 1/15360 或 1/90000），FLV 是 1/1000（毫秒）。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（散见于 h264_mp4 系列）

---

### Q20. time_base 是什么？为什么 FFmpeg 里到处都是时间基转换？

**面试官提问**："你拿到一个 packet 的 pts=3600，这是什么时间？怎么换成秒？"

**标准答案**：**time_base** 是"时间戳的单位"，是个分数（AVRational：num/den）。pts=3600、time_base=1/90000 意味着 3600 × (1/90000) = 0.04 秒。**FFmpeg 里到处转换的原因**：每一层 time_base 不一样——容器有 `AVStream.time_base`（MP4 常见 1/15360）、解码器有 `AVCodecContext.time_base`（视频常见 1/帧率）、编码器有自己的 time_base、ScaleSource 又有自己的。**跨层传递 pts 必须用 `av_rescale_q` 换算**：`out_pts = av_rescale_q(in_pts, in_time_base, out_time_base)`，否则要么播放速度爆炸（快/慢几倍），要么音画完全失同步。**面试坑**：转码场景 `pkt.pts = av_rescale_q(pkt.pts, in_stream->time_base, out_stream->time_base)` 是写转码代码必踩的雷点。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（散见，建议专题补一份）

---

### Q21. 音画同步的三种策略？分别什么场景用？

**面试官提问**："你做播放器，视频和音频不同步怎么办？谁追谁？"

**标准答案**：三种策略对应三种"主时钟"：① **视频追音频（最常用）**：以音频时钟为主——音频按采样率自然走、解码渲染严格匹配；视频解码后看 `video_pts vs audio_clock`：超前则等、落后超过 40ms 则丢帧（或快放追上）。**适合点播/直播**——人耳对音频中断/变速极度敏感，对视频丢帧不敏感。② **音频追视频**：以视频时钟为主——音频要变速（重采样）或丢/补样追上。**适合无音轨场景**（监控、纯视频）。③ **都追外部时钟（系统时间）**：双方都向系统墙钟对齐。**RTC 场景常用**——两路都需要严格控制延迟、且双方时钟独立（NTP 同步过）。**面试一句话答**："90% 场景视频追音频；RTC/直播两路独立时用墙钟。"

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/预览延时/`（可能有）

---

### Q22. 视频帧率不稳定（VFR）怎么处理 PTS？

**面试官提问**："你录屏的视频帧率忽快忽慢，怎么保证播放时不卡顿？"

**标准答案**：**CFR（恒定帧率）** 每帧 PTS 间隔固定（25fps 即每帧 40ms），编码效率最高、播放最稳；**VFR（可变帧率）** 每帧 PTS 间隔不固定（屏幕静止时不出帧、动作场景密集出帧），优势是节省码率。处理 VFR 的关键：① **采集端**给每帧打**真实时间戳**（基于 `CACurrentMediaTime` / `std::chrono::steady_clock`），不要按"帧序号 × 帧间隔"算 PTS；② **编码器**用 `force_key_frames` + `keyint=动态` 配置，避免长时间无 I 帧；③ **播放端**严格按 PTS 渲染（不能按"每 40ms 出一帧"假设）；④ **转码场景**如果目标要 CFR，要做**帧补齐（duplicate）/ 抽帧（drop）**，FFmpeg 用 `-vsync cfr` 或 `setpts/fps` 滤镜。**面试坑**：直播推流给到 CDN 必须是 CFR，VFR 会让 HLS 切片时长漂移。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（空白，建议补）

---

### Q23. 什么是 wraparound？32 位 PTS 溢出会怎样？

**面试官提问**："你解析 MPEG-TS 跑了几十小时后突然 PTS 变小了，怎么回事？"

**标准答案**：MPEG-TS 的 PTS 字段是 **33 位**（90kHz 时基），最大值 `2^33 - 1`，约 26.5 小时溢出归零（wraparound）。RTP 的 timestamp 字段是 **32 位**（音频通常 48kHz、视频 90kHz），溢出更快——视频 32 位 90kHz 约 13.25 小时归零、音频 48kHz 约 24.85 小时。**处理方式**：维护"上一次 PTS"，发现新 PTS 突然变小且差值大于某阈值（例如 2^31），认为是 wraparound、不是回退，把高位 +1。FFmpeg 的 `av_pkt_dump_log2` 内部就有这个逻辑。**面试坑**：① 直播流跑超过 13/26 小时必踩；② RTP 时间戳的**初始值随机**（防攻击），不是从 0 开始——所以不能用绝对值算时长，必须用 delta；③ 同一个 SSRC 内 wraparound 要处理，切换 SSRC 后时间戳完全独立。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（空白，建议补）

---

### Q24. 视频丢帧和音频丢帧策略有什么不同？

**面试官提问**："播放卡了，视频和音频都要丢，谁丢得更激进？"

**标准答案**：**视频可以激进丢、音频几乎不能丢**。原因：① 人耳对音频不连续极度敏感（卡顿/爆音立刻察觉），对视频 30→24fps 几乎无感；② 视频丢帧只要保留参考链——P 帧不能丢（后续帧解不出来）、B 帧可以丢（无依赖）；丢到一定程度直接快进到下一个 IDR。**音频**几乎只能"加速播放追时钟"（变速不变调，用 SoundTouch / WSOLA 算法），实在追不上才小幅丢样本（< 10ms 不易察觉）。**WebRTC 的具体策略**：视频用 Jitter Buffer 缓冲，缓冲过深就丢非参考帧；音频 NetEQ 用时域伸缩（time-stretching）加速/减速 ±25% 不变调。**面试一句话答**："视频丢帧到关键帧重启，音频用变速算法吸收抖动。"

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（空白，建议补）

---

## 五、FFmpeg 核心 API（6 题）

### Q25. AVPacket 和 AVFrame 的区别？它们的生命周期由谁管？

**面试官提问**："你 `av_read_frame` 出来一个 packet，用完后该怎么释放？不释放会怎样？"

**标准答案**：**AVPacket** 装"编码后的压缩数据"（一个 packet ≈ 一帧 H264 NALU 或一帧 AAC 数据），由 `av_packet_alloc` 分配壳、`av_read_frame` / `avcodec_receive_packet` 填充内容；**AVFrame** 装"解码后的原始数据"（YUV/PCM），由 `av_frame_alloc` 分配壳、`avcodec_receive_frame` 填充。两者的 buffer 都是**引用计数的**——`av_packet_unref` / `av_frame_unref` 释放当前持有的数据（引用计数 -1），`av_packet_free` / `av_frame_free` 释放壳本身。**典型循环**：`while (av_read_frame(...)) { 处理 pkt; av_packet_unref(&pkt); }`。**忘记 unref 会内存泄漏**（每次循环新申请 buffer，引用计数从未归零）。**面试常考的坑**：`av_packet_ref` 是浅拷贝（共享 buffer、计数+1），`av_packet_clone` 是分配新壳+ref。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/avframe_avpacket_guide.md`、`Doc/ffmpeg/ffmpeg_resource_lifecycle_notes.md`

---

### Q26. `avcodec_send_packet` / `avcodec_receive_frame` 的 send-receive 模式怎么用？为什么是异步的？

**面试官提问**："为什么解码 API 不是 `decode_frame(packet) → frame`，而是要分两步？"

**标准答案**：FFmpeg 从 3.1 开始把同步 API（`avcodec_decode_video2`）改成异步 send-receive 模式。原因：① **支持帧重排序**（B 帧场景一个 send 不一定立即对应一个 receive）；② **支持 batch 处理**（一次 send 多个、一次 receive 多个，硬件解码效率更高）；③ **支持 EOF 信号**（send NULL 触发解码器吐出残留帧）。**典型循环**：

```
while (av_read_frame(fmt, pkt) >= 0) {
    avcodec_send_packet(codec, pkt);
    while (avcodec_receive_frame(codec, frame) == 0) {
        处理 frame;
        av_frame_unref(frame);
    }
    av_packet_unref(pkt);
}
// 刷出残留
avcodec_send_packet(codec, NULL);
while (avcodec_receive_frame(codec, frame) == 0) { ... }
```

返回值 `EAGAIN` 表示"send 满了，先 receive"或"receive 没有"。**面试坑**：忘记最后 send NULL 会丢失末尾几帧（B 帧场景）。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/avframe_avpacket_guide.md`

---

### Q27. AVFormatContext / AVCodecContext / AVCodec 的关系？

**面试官提问**："你 `avformat_open_input` 之后到 `avcodec_open2` 之间走了哪几步？"

**标准答案**：**AVFormatContext** 是容器级上下文（一个文件/流的所有元信息），含多个 `AVStream`；**AVStream** 是单路流（视频/音频/字幕之一），含 codecpar（编码参数）和 time_base；**AVCodec** 是只读的"编解码器静态描述"（H264 解码器 / AAC 编码器），通过 `avcodec_find_decoder(codec_id)` 找到；**AVCodecContext** 是可写的"解码器实例上下文"，由 `avcodec_alloc_context3(codec)` 创建。**标准流程**：

```
avformat_open_input(&fmt, file) → 打开容器
avformat_find_stream_info(fmt)  → 探测流信息
找到视频流 idx = av_find_best_stream(fmt, VIDEO, ...)
AVCodec *codec = avcodec_find_decoder(fmt->streams[idx]->codecpar->codec_id)
AVCodecContext *ctx = avcodec_alloc_context3(codec)
avcodec_parameters_to_context(ctx, fmt->streams[idx]->codecpar)
avcodec_open2(ctx, codec, NULL) → 真正初始化解码器
```

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/ffmpeg_avcodec_vs_context.md`

---

### Q28. swscale 和 swresample 分别做什么？什么时候用？

**面试官提问**："你解码出来 YUV420P，要渲染成 RGBA，怎么转？"

**标准答案**：**swscale**（`libswscale`）做**视频像素格式 + 分辨率**转换：YUV420P → RGBA、1920×1080 → 1280×720、stride 对齐调整。核心 API：`sws_getContext(srcW, srcH, srcFmt, dstW, dstH, dstFmt, flags, ...)` 创建上下文，`sws_scale(ctx, srcSlice, srcStride, ..., dstSlice, dstStride)` 执行转换。**swresample**（`libswresample`）做**音频采样格式 + 采样率 + 通道布局**转换：S16 交错 → FLTP 平面、44100Hz → 48000Hz、单声道 → 立体声。核心 API：`swr_alloc_set_opts` 创建、`swr_init`、`swr_convert(ctx, out, out_count, in, in_count)`。**两者都是 CPU 实现**（有 SIMD 优化），高性能场景用 GPU shader（视频）/ SoundTouch（音频变速）替代。**面试坑**：swscale 的 `flags=SWS_BILINEAR/SWS_BICUBIC` 影响画质，缩放时选错会糊。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/swscontext_lecture.md`

---

### Q29. FFmpeg 的引用计数（ref-counted buffer）是怎么工作的？避免哪些坑？

**面试官提问**："你 `av_packet_ref` 和 `av_packet_clone` 的区别？"

**标准答案**：FFmpeg 4.x+ 的 AVPacket/AVFrame 内部都用 `AVBufferRef` 管理数据 buffer，引用计数自动维护。**核心 API**：

- `av_packet_ref(dst, src)`：浅拷贝壳 + buffer 引用计数 +1（**dst 和 src 共享 buffer**）；
- `av_packet_clone(src)`：等价于 alloc + ref，返回新壳；
- `av_packet_unref(pkt)`：buffer 计数 -1，归零时释放数据，pkt 壳本身保留；
- `av_packet_free(&pkt)`：释放壳本身（内部会调 unref）；
- AVFrame 同理。

**典型坑**：① 把 AVPacket 塞进队列前要 `av_packet_ref` 一份（直接拷贝结构体只复制了指针，原 pkt unref 后队列里的指针变野）；② 多线程消费同一份 frame 必须各自 ref 持有引用，不能裸传指针；③ 自己分配的 buffer 通过 `av_buffer_create(data, size, free_cb, ...)` 包装成 AVBufferRef 接入引用计数体系。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/ffmpeg_resource_lifecycle_notes.md`

---

### Q30. 硬件解码（VideoToolbox / VAAPI / NVDEC）和软解的核心差异？

**面试官提问**："你想用硬解，怎么知道当前 codec 支持？硬解出来的 frame 怎么拿到 CPU 内存？"

**标准答案**：**软解**用 CPU 跑解码算法（libavcodec 自带），跨平台、稳、可控，但 4K/60fps 时 CPU 占用高（一个核全占）。**硬解**用 GPU/专用 ISP 跑，CPU 占用 < 10%，但**输出格式是 GPU 显存里的纹理**（VideoToolbox 是 CVPixelBuffer、VAAPI 是 vaSurface、NVDEC 是 CUdeviceptr），不能直接读。**用法**：① 先 `avcodec_get_hw_config(codec, idx)` 枚举支持的硬件类型；② 创建 `AVBufferRef *hw_device_ctx`（`av_hwdevice_ctx_create`）；③ 设置 `codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx)`；④ 设置 `get_format` 回调返回硬件像素格式；⑤ 解码出的 AVFrame 的 `format` 是硬件格式（如 `AV_PIX_FMT_VIDEOTOOLBOX`），用 `av_hwframe_transfer_data(sw_frame, hw_frame, 0)` 拷回 CPU。**实战常用**：如果只是要送渲染，**保持 GPU 内存零拷贝**直接给 OpenGL/Metal 用，性能最高。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/hardware_codec_learning_guide.md`

---

## 六、直播协议对比（5 题）

### Q31. RTMP / HLS / WebRTC / SRT 的延迟和适用场景对比？

**面试官提问**："直播 1v1 互动用什么？秀场直播 1 万人观看用什么？为什么？"

**标准答案**：**RTMP（实时消息协议）**：基于 TCP，端到端延迟 2-5 秒，CDN 友好（推流用），观看端通常转 HLS。**HLS（HTTP Live Streaming）**：HTTP 短连接拉切片（.ts），延迟 10-30 秒（受切片时长 × 3 影响），最适合**大规模观看分发**（CDN 缓存友好）、苹果系兼容性最佳。**WebRTC**：基于 UDP + SRTP + ICE，**端到端延迟 < 500ms**（典型 100-200ms），P2P 或经 SFU 中转，**适合 1v1 / 小房间互动**（视频通话、互动连麦）。**SRT（Secure Reliable Transport）**：基于 UDP，自实现可靠传输 + 加密，延迟 0.5-2 秒，**适合广播级跨城传输**（推流到主控、远程导播）。**一句话答**：① 1v1 互动 = WebRTC；② 秀场带连麦 = WebRTC（连麦）+ RTMP 推 CDN 转 HLS（围观）；③ 纯直播大规模 = RTMP 推 + HLS 拉。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/网络协议通关指南.md`、`learnTarget/WebRTC实战指南.md`

---

### Q32. 为什么 WebRTC 用 UDP 不用 TCP？

**面试官提问**："TCP 可靠传输不是更好吗？为什么实时通信坚持用 UDP？"

**标准答案**：核心是**"实时性 vs 可靠性的取舍"**。① TCP 的队头阻塞：丢一个包后续包必须等重传到达才能往上层投递——网络抖动时延迟雪崩；UDP 没有这个约束，应用层自己决定丢/重传。② TCP 的拥塞控制偏保守：检测到丢包就降速 50%（cubic/reno），不适合视频流"宁可糊一会儿也要保住带宽"。③ 重传 vs 跳过：实时通信里**晚到的包 = 没用的包**（已经过了显示时刻），TCP 强行重传只会让后续更晚，UDP 直接丢、用 FEC/NACK 选择性恢复关键帧。④ **包大小可控**：UDP 应用层决定 MTU 拆包，避开 TCP Nagle 算法的延迟合并。**RTC 实际方案**：UDP + SRTP（加密）+ RTCP（统计反馈）+ FEC/NACK（选择性可靠）+ 拥塞控制（GCC/BBR）。**回退方案**：UDP 被防火墙 ban 时回退到 TURN over TCP。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/网络协议通关指南.md`（部分）

---

### Q33. HLS 的延迟主要来自哪里？怎么把 HLS 优化到秒级？

**面试官提问**："HLS 默认延迟 20 秒，怎么搞到 3 秒以内？"

**标准答案**：HLS 默认延迟 = **切片时长 × 缓冲分片数**，例如 6 秒切片 × 3 个缓冲 = 18 秒。优化思路：① **缩短切片时长**：6 秒 → 1 秒（要求 GOP 长度也对应缩短，影响压缩效率），代价是切片数变多、CDN 缓存命中率下降；② **LL-HLS（Low-Latency HLS）**：苹果 2019 引入，把切片再拆成 200ms 的 partial segments，用 HTTP/2 push 推下来，端到端延迟可降到 2-3 秒；③ **CMAF + chunked transfer**：用 fMP4 chunked encoding 边生成边推、播放器边下边播，延迟可降到 1-2 秒；④ **WebRTC + 自研信令** 直接抛弃 HLS。**面试坑**：① 切片越短 CDN 回源压力越大、切片文件数指数级增长；② LL-HLS 要求服务器和 CDN 都支持 HTTP/2 push，目前覆盖一般。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（空白，建议补）

---

### Q34. RTP 包的 header 字段有哪些？你能默写出来吗？

**面试官提问**："你说一下 RTP 头有哪些字段、各占几位、分别干什么用？"

**标准答案**：RTP 头**12 字节固定**（不含扩展），字段：

- V（2 bit）：版本号，固定 2；
- P（1 bit）：padding 标志；
- X（1 bit）：扩展头存在标志；
- CC（4 bit）：CSRC 数量；
- M（1 bit）：标记位（视频中表示帧边界、音频中表示静音起始）；
- PT（7 bit）：payload type，标识载荷类型（96=动态范围，配合 SDP 协商）；
- Sequence Number（16 bit）：序列号，每包 +1，**接收端用于丢包检测和重排**；
- Timestamp（32 bit）：时间戳，单位由 PT 决定（视频 90kHz、Opus 48kHz）；
- SSRC（32 bit）：同步源标识，**唯一标识一路流**（同一会话不同流的 SSRC 必须不同）；
- CSRC 列表（每个 32 bit，可选）：贡献源（混音/合流场景列出原始 SSRC）；
- 扩展头（可选）：含 RFC 8285 的 one-byte/two-byte 头扩展（transport-cc 序号在这里）。

**面试常考的追问**：① M 位在 H264 中怎么用？标记一帧的最后一个 RTP 包；② SSRC 冲突怎么办？检测到自动换 SSRC + 发 BYE。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（空白，WebRTC 阶段三必补）

---

### Q35. SDP 是什么？Offer / Answer 流程怎么走？

**面试官提问**："WebRTC 建连为什么要先交换 SDP？SDP 里有什么？"

**标准答案**：**SDP（Session Description Protocol）** 是文本格式的会话描述协议，描述"我能发什么、我能收什么"（编码器列表、传输地址、加密参数、ICE candidate）。**Offer/Answer 流程**：① A 调 `createOffer` 生成 SDP（含 A 支持的所有编码 + ICE candidate + DTLS 指纹），通过**业务信令通道**（WebSocket/HTTPS）发给 B；② B 调 `setRemoteDescription(offer)` 解析 A 的能力，然后 `createAnswer` 生成自己的 SDP（在 A 的能力子集里选定最终参数），发回 A；③ A 调 `setRemoteDescription(answer)` 完成协商；④ 同时双方进行 **ICE candidate 收集和连通性检查**，建立 UDP 通道；⑤ DTLS 握手协商 SRTP 密钥；⑥ 媒体开始流动。**面试常追问**：为什么要 Offer/Answer 不是单向声明？因为双方能力可能不对称，需要协商交集；为什么 SDP 走业务信令不走 WebRTC 自己？因为还没建连。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`阶段一-WebRTC架构总览.md`（部分）

---

## 七、音频处理基础（5 题）

### Q36. 3A（AEC / AGC / ANS）分别是什么？解决什么问题？

**面试官提问**："开免提通话听到自己说话有回声，怎么处理？"

**标准答案**：**AEC（Acoustic Echo Cancellation，回声消除）**：扬声器播放的远端声音被本地麦克风采集，远端听到自己回声——AEC 通过自适应滤波（NLMS / RLS）估计回声路径、从麦克风信号中减去。WebRTC 用 AEC3。**AGC（Automatic Gain Control，自动增益）**：说话距离麦克风远近不同导致音量波动——AGC 动态调整增益让输出音量稳定。两种模式：数字 AGC（软件调）和模拟 AGC（驱动调麦克风电平）。**ANS / NS（Noise Suppression，噪声抑制）**：消除空调声、键盘声等稳态噪声，传统用谱减法 / Wiener 滤波，现代用 RNN（RNNoise / WebRTC ML-NS）。**面试一句话答**："AEC 消回声，AGC 调音量，ANS 去噪声——三者串联在采集后、编码前，WebRTC 里都在 APM（AudioProcessingModule）里。"

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（空白，WebRTC 阶段三必补）

---

### Q37. 音频重采样为什么需要？常见坑？

**面试官提问**："你的麦克风采集是 44.1kHz，编码器要 48kHz，怎么转？"

**标准答案**：**重采样（resampling）** 是把音频从一个采样率转到另一个（44.1kHz → 48kHz），核心算法是**多相滤波**（先升采样到 LCM 频率、低通滤波、再降采样）。FFmpeg 用 `swr_convert`、独立库可用 SOX / Speex resampler。**常见坑**：① **不是整数倍很麻烦**：44.1k → 48k 比例 160:147，需要先升采样到 7056k 再降——计算量大、引入延迟（几十样本）；② **质量参数**：FFmpeg 的 `swr_init` 前用 `av_opt_set_int(swr, "filter_size", 32, 0)` 调滤波器长度，长 = 质量高但 CPU 高；③ **跨帧状态**：重采样器内部有滤波延迟和 buffer，**多个 chunk 必须用同一个 swr 上下文连续调**，不能每次新建（边界会有 click 噪声）；④ **同时改采样率 + 通道布局 + 格式** 用一个 swr 处理就够。**WebRTC 的做法**：APM 内部强制把所有输入归一到 48kHz / 16bit / 单声道（叫 capture format），3A 之后再转目标格式。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/avsampleformat_interview_guide.md`（部分）

---

### Q38. 什么是 NetEQ？它解决什么问题？

**面试官提问**："WebRTC 音频接收端为什么不能像视频一样固定 jitter buffer？"

**标准答案**：**NetEQ（Network Equalizer）** 是 WebRTC 的音频自适应抖动缓冲 + 丢包隐藏模块，比视频 jitter buffer 复杂得多。**为什么需要**：音频不能像视频那样"丢一帧没关系"——丢音频帧人耳立刻察觉。NetEQ 同时做四件事：① **抖动估计**：实时估计网络抖动分布，动态调整 buffer 深度（典型 60-200ms）；② **PLC（Packet Loss Concealment）**：丢包时用前一包推断生成替代音频（基于线性预测 LPC）；③ **时域伸缩（Time-stretching）**：当缓冲过深时**加速播放追上**（不变调）、过浅时**减速等待**——±25% 内人耳几乎无感；④ **Comfort Noise（CNG）**：静音段补充舒适背景噪声。**面试一句话答**："NetEQ = 抖动缓冲 + 丢包隐藏 + 变速不变调 + 舒适噪声，源码在 `modules/audio_coding/neteq/`。"

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（空白，WebRTC 阶段三必补）

---

### Q39. 采样率为什么常见 44.1kHz / 48kHz / 16kHz？

**面试官提问**："为什么 CD 是 44.1kHz、电话是 8kHz、WebRTC 是 48kHz？"

**标准答案**：根据**奈奎斯特定理**，采样率至少是信号最高频率的 2 倍才能无损还原。**人耳听音范围 20Hz-20kHz**，所以无损音频要 ≥ 40kHz。具体值：① **44.1kHz**（CD 标准）：历史原因，1980 年代 PCM 数字录音设备基于电视行频（NTSC 60Hz × 245 行 × 3 样本 ≈ 44.1k）；② **48kHz**（专业音视频/电影/WebRTC）：和视频帧率整除关系好（48000/24=2000、/30=1600），无相位偏移；③ **16kHz**（语音）：人声主要能量在 8kHz 以下，16kHz 够还原（VoIP/语音识别标配）；④ **8kHz**（窄带电话）：早期电话网（PSTN）带宽限制；⑤ **96kHz / 192kHz**（高保真）：超出人耳范围，主要给后期处理用。**WebRTC 默认 48kHz**：兼容专业流水线、和 Opus 原生匹配。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：（基础知识，散见）

---

### Q40. 单声道 / 立体声 / 5.1 怎么编码？通道排列顺序是怎样的？

**面试官提问**："你做 5.1 声道的播放器，6 个声道在 PCM 里怎么排？"

**标准答案**：**单声道（Mono）** 1 通道；**立体声（Stereo）** 2 通道：L、R；**5.1 环绕** 6 通道，标准排列（WAVE/SMPTE）：FL（左前）、FR（右前）、FC（中置）、LFE（重低音）、BL（左后）、BR（右后）；**7.1** 8 通道：5.1 基础上加 SL/SR（侧环绕）。**两种内存布局**：① **交错（packed）**：LRLRLR... 或 FL/FR/FC/LFE/BL/BR/FL/FR/...，每个样本含所有通道（适合驱动输出）；② **平面（planar）**：所有 FL 样本连续、所有 FR 样本连续、... 共 N 个平面（适合编码器输入）。**FFmpeg 用 AVChannelLayout 描述**（旧版 channel_layout 已弃用），常量 `AV_CHANNEL_LAYOUT_5POINT1`。**面试坑**：① 编码 AAC 必须用 planar（FLTP），从驱动拿的是 packed，要 `swr_convert` 转一次；② 立体声混单声道不是 `(L+R)/2` 就完事，要考虑相位反向问题（有些母带 L+R 会抵消）。

**自检**：你能口述清楚吗？[ ] Y / [ ] N
**已有文档**：`Doc/ffmpeg/avsampleformat_interview_guide.md`（部分）

---

## 八、知识地图（已覆盖 vs 空白）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│           C++ 音视频面试基础知识地图 · 阶段零 self-check                     │
│  图例：[✓] 已有文档；[~] 部分覆盖；[ ] 空白需补；★ WebRTC 阶段三必补        │
└─────────────────────────────────────────────────────────────────────────────┘

一、像素与采样格式 (Q1-Q5)
  [✓] YUV420P 布局              → Doc/ffmpeg/pixel_format_memory_layout_guide.md
  [✓] NV12/NV21                 → 同上
  [✓] PCM 字节计算              → Doc/ffmpeg/avsampleformat_interview_guide.md
  [✓] stride / 行对齐           → pixel_format_memory_layout_guide.md
  [✓] RGB↔YUV 损失              → Doc/ffmpeg/swscontext_lecture.md

二、编解码码流结构 (Q6-Q13)
  [✓] H264 NALU                 → Doc/ffmpeg/h264_mp4_面试速记.md
  [~] SPS/PPS                   → h264_mp4_模拟面试.md（基础覆盖，深度可补）
  [✓] Annex-B vs AVCC           → mp4-2-ts.md
  [~] H264/H265/VP9/AV1 对比    → 码率-Profile.md（部分）
  [ ] I/P/B 帧延迟              → 散见，建议专题汇总
  [ ] AAC ADTS / ADIF           ★ 空白，WebRTC 阶段三补
  [ ] Opus 编码器               ★ 空白，WebRTC 阶段三必补
  [✓] QP/CRF/CBR/VBR            → Doc/ffmpeg/码率-Profile.md

三、容器与封装 (Q14-Q18)
  [~] FLV/MP4/TS/WebM/MKV       → mp4-2-ts.md（FLV/MP4/TS 覆盖，WebM/MKV 空白）
  [✓] MP4 moov / faststart      → Doc/ffmpeg/mp4_h264_mock_interview.md
  [~] MPEG-TS 188/PCR           → mp4-2-ts.md（PCR 深度可补）
  [~] HLS 自包含切片            → 部分覆盖于 mp4-2-ts.md
  [ ] FLV Tag 结构              空白，建议补

四、时间戳与音画同步 (Q19-Q24)
  [~] PTS/DTS                   → 散见于 h264_mp4 系列
  [ ] time_base 换算            空白，建议专题补
  [ ] 音画同步三策略            → Doc/预览延时/（可能有，待确认）
  [ ] VFR PTS 处理              空白
  [ ] PTS wraparound            空白
  [ ] 音视频丢帧策略            空白

五、FFmpeg 核心 API (Q25-Q30)
  [✓] AVPacket / AVFrame        → Doc/ffmpeg/avframe_avpacket_guide.md
  [✓] send/receive 模式         → 同上
  [✓] FormatCtx/CodecCtx        → Doc/ffmpeg/ffmpeg_avcodec_vs_context.md
  [✓] swscale / swresample      → Doc/ffmpeg/swscontext_lecture.md
  [✓] 引用计数                  → Doc/ffmpeg/ffmpeg_resource_lifecycle_notes.md
  [✓] 硬解硬编                  → Doc/ffmpeg/hardware_codec_learning_guide.md

六、直播协议对比 (Q31-Q35)
  [✓] RTMP/HLS/WebRTC/SRT 对比  → Doc/ffmpeg/网络协议通关指南.md
  [~] WebRTC 选 UDP 的原因      → 网络协议通关指南.md（部分）
  [ ] HLS 低延迟优化            空白
  [ ] RTP Header 字段           ★ 空白，WebRTC 阶段三必补
  [~] SDP / Offer-Answer        → 阶段一-WebRTC架构总览.md

七、音频处理基础 (Q36-Q40)
  [ ] 3A (AEC/AGC/ANS)          ★ 空白，WebRTC 阶段三必补
  [~] 重采样 swr                → avsampleformat_interview_guide.md（部分）
  [ ] NetEQ                     ★ 空白，WebRTC 阶段三必补
  [ ] 采样率历史                空白
  [~] 多通道布局                → avsampleformat_interview_guide.md（部分）
```

### 统计

- **已有文档覆盖较好（✓ + ~）**：25 / 40 题（62.5%）
- **完全空白需要补（[ ]）**：15 / 40 题（37.5%）
- **WebRTC 阶段三必补（★）**：5 题（AAC ADTS / Opus / RTP Header / 3A / NetEQ）——这些会在阶段三对应模块下钻时补，不必现在单独写文档。

### 时间盒建议

- 第 1 天：先把所有题闭卷自测一遍，标 Y / N。
- 第 2-3 天：N 项里**先补 [ ] 空白且非★**的（时间戳系列、FLV Tag、HLS 优化），这些是 WebRTC 之外的面试题，必须在阶段三之前掌握。
- 第 4 天：N 项里 [~] 部分覆盖的回头再读已有文档（不用新写）。
- 第 5 天：交付 N→Y 的总结，进入阶段二。

---

## 结束语

阶段零完成。请你按上面的清单**做一遍自检**（每题标 Y / N），然后告诉我：

1. **你打 N 的题号有哪些？** 数量超过 10 个的话，我们先针对性补几个核心题（例如时间戳系统、AAC ADTS），再进阶段二；数量少于 5 个，可以直接进阶段二。
2. **★ 标记的 5 个题**（AAC ADTS / Opus / RTP Header / 3A / NetEQ）打算放到阶段三 WebRTC 对应模块下钻时补，还是现在就单独补？
3. **是否进入阶段二**：项目设计方案（A 层技术栈选型 + B 层模块选定）？

