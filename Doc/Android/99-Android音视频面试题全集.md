# Android 音视频面试题全集：中高级岗位

## 0. 本篇定位

- 面试复习：这是 Android 音视频的集中题库，适合考前按模块快速扫题，优先补齐 MediaCodec、Camera2、OpenGL ES、音频和端到端推流。
- 深入学习：每道题都应回到对应专题文档验证底层原因，避免只背结论。
- 使用方法：能口述主链路、能解释一次真实踩坑、能说出性能指标和排查工具，才算达到中高级回答质量。
> **适用方向**：Android 移动端音视频 SDK 开发（采集/编解码/渲染/推流 全链路）
> **适用级别**：中级（3-5年）、高级（5年+）
> **前置知识**：本系列 00-06 全部文档 · [[../ffmpeg/14-Android硬件编解码]] · [[../ffmpeg/00-FFmpeg全景导读]]
> **定位**：🔥🔥🔥 面试前最后一遍的系统复习

---

## 一、MediaCodec 编解码（8 题）

### Q1：MediaCodec 的缓冲区模型是怎样的？同步和异步有什么区别？

**难度/频率**：⭐⭐ / 🔥🔥🔥

> MediaCodec 是两条队列模型——输入队列和输出队列。同步模式自己调 dequeueInputBuffer/dequeueOutputBuffer，传 timeout 轮询——简单但有空转。异步模式 setCallback 注册回调——当 buffer 可用时系统回调你，没有忙等，推荐生产用。但 setCallback 必须在 configure 之前调。核心铁律：output buffer 用完后必须 releaseOutputBuffer 还回去——buffer 总数固定、在两条队列间轮转，拿了不还池子耗尽编解码卡死。

### Q2：MediaCodec 怎么区分硬解和软解？

**难度/频率**：⭐⭐ / 🔥🔥🔥

> 同一套 API（MediaCodec），区别在选用的 codec 组件名。OMX.qcom./OMX.MTK./c2.qti./c2.mtk. 前缀是硬件——对应高通/联发科的 VPU。OMX.google./c2.android. 前缀是软件——跑在 CPU 上。API 29+ 可以直接 codecInfo.isHardwareAccelerated() 判断。MediaCodec 是门面、它自己不干活——具体是硬是软取决于底层被选中的组件。

### Q3：MediaCodec 解码花屏，最常见的原因是什么？

**难度/频率**：⭐⭐⭐ / 🔥🔥🔥

> ByteBuffer 模式下颜色格式 + stride + sliceHeight 三个问题层层递进。第一：YUV 颜色格式因芯片而异——I420/NV12/NV21/私有 tiled，按 NV12 方式读 I420 数据直接花屏。第二：就算格式对了，每行有对齐填充——stride ≠ width，按 width 紧凑读必然错位偏绿。第三：sliceHeight 也不等于 height，Y 平面和 UV 平面之间有对齐间隔——UV 起点算错就是满屏雪花。最简单的修复是换成 Surface 输出——所有这些问题 GPU 端自动解决。

### Q4：SPS/PPS 在 Android 上怎么传？和 iOS 有什么不同？

**难度/频率**：⭐⭐⭐ / 🔥🔥

> Android 解码 configure 时在 MediaFormat 里设 "csd-0"=SPS、"csd-1"=PPS——传入的是纯 NALU 数据（去掉起始码 00 00 00 01）。iOS 要用 CMVideoFormatDescriptionCreateFromH264ParameterSets 创建格式描述对象后才能建 VTDecompressionSession——不同的 API，但本质相同：都是把参数集和帧数据分开传给编解码器。编码方向：Android 编码器自产 SPS/PPS 放在 Annex-B 流里，iOS VT 编码器的 SPS/PPS 单独存 CMVideoFormatDescription。Android 吐 Annex-B（起始码），iOS 吐 AVCC（长度前缀）——这是跨平台最易翻车的点。

### Q5：怎么动态改码率？怎么强制关键帧？

**难度/频率**：⭐⭐ / 🔥🔥

> 动态改码率：Bundle params = new Bundle(); params.putInt(KEY_VIDEO_BITRATE, newBitrate); codec.setParameters(params); 强制关键帧：params.putInt(PARAMETER_KEY_REQUEST_SYNC_FRAME, 0); codec.setParameters(params); 下一个编码帧就是 IDR。这两个就是 WebRTC 拥塞控制 + PLI 恢复的 Android 实现。

### Q6：OMX 和 Codec2 有什么区别？

**难度/频率**：⭐⭐⭐ / 🔥🔥（高级常问）

> OMX 是 Khronos 老标准，Android 9 及以前用。buffer 模型跟 gralloc/dma-buf 衔接别扭、厂商扩展乱、早期跑在 mediaserver 一崩全崩。Codec2 是 Android 10 Google 重写的——原生围绕 dma-buf 设计、零拷贝衔接顺、跑在独立沙箱崩了不拖垮系统。App 端的 MediaCodec API 没变——只是组件名前缀 OMX.* → c2.*。

### Q7：COLOR_FormatYUV420Flexible 和 COLOR_FormatSurface 的区别？

> COLOR_FormatYUV420Flexible：ByteBuffer 模式，YUV 数据在 CPU 可读的 ByteBuffer 里——但实际颜色格式因设备而异（NV12/NV21/I420/tiled），需手动处理。COLOR_FormatSurface：Surface 模式，YUV 数据在 GPU 显存——零拷贝，无颜色格式坑，无 stride 坑。结论：纯播放选 Surface，需后处理(Bytedance 美颜/AI)选 ByteBuffer。

### Q8：MediaCodec 的 releaseOutputBuffer 第二个参数 true/false 什么区别？

> true（Surface 模式）：渲染到 output Surface——零拷贝，GPU 直接消费。false（ByteBuffer 模式）：不做渲染，由你用 ByteBuffer/Image 读数据后手动 release 回池子。true 会触发 Surface 的消费者（SurfaceFlinger/GL 纹理）更新画面。

---

## 二、Camera2 采集（3 题）

### Q9：Camera2 怎么拿到每一帧 YUV 数据？

**难度/频率**：⭐⭐ / 🔥🔥🔥

> 用 ImageReader：new ImageReader(w, h, YUV_420_888, maxImages=3-4)，setOnImageAvailableListener。回调里 image.getPlanes() 拿 Y/U/V 三平面的 ByteBuffer。关键：YUV_420_888 不保证是 I420/NV12/NV21——必须根据 planes.length（3=I420, 2=NV12/NV21）和 pixelStride 动态判断格式，必须用 rowStride 逐行正确拷贝。Image 用完必须 close()。

### Q10：YUV_420_888 的 planes 怎么正确拷贝？

> 三个平面。plane[0]=Y：pixelStride 总是 1，每行 rowStride 字节（可能 >width），只拷贝前 width 字节。plane[1]=U 或 UV（取决于格式）：pixelStride=1 是 I420 的 U、pixelStride=2 是 NV12 的 UV 交织。plane[2] 存在=I420、null=NV12/NV21。NV12 的 UV 顺序是 U,V,U,V…，NV21 是 V,U,V,U…。按 pixelStride 和 rowStride 逐个像素正确读取。

### Q11：Camera2 的 Surface 通路 vs ImageReader 通路怎么选？

> Surface：零拷贝——Camera HAL 直接输出到 GPU 纹理，不经过 CPU。适合纯预览或给 MediaCodec 直接编码。ImageReader：CPU 可读——拿到 YUV 裸数据。适合需要美颜/AI 分析的场景。可以同时用多个 Surface——同一个 CaptureSession 可以有多个输出 target。

---

## 三、OpenGL ES 渲染（3 题）

### Q12：SurfaceTexture 是什么？怎么用？

**难度/频率**：⭐⭐⭐ / 🔥🔥

> SurfaceTexture 是把 Surface（Camera/MediaCodec 的 GPU 输出）转为 OpenGL OES 纹理的桥梁。创建时传入 OES texture ID → 调 updateTexImage() 把最新的 Surface 帧更新到 OES 纹理 → fragment shader 里用 samplerExternalOES 采样 → YUV→RGB。getTransformMatrix() 返回旋转/缩放矩阵——必须乘到纹理坐标上否则方向不对。

### Q13：OES 纹理和普通 GL_TEXTURE_2D 有什么区别？

> 采样器必须用 samplerExternalOES 而不是 sampler2D。shader 必须声明 #extension GL_OES_EGL_image_external : require。不支持 mipmap、不支持 GL_REPEAT、只能 GL_CLAMP_TO_EDGE。OES 纹理由外部（Camera/MediaCodec）生产和更新——你的 GL 代码不能直接 upload 数据。

### Q14：GLSurfaceView 和 TextureView 怎么选？

> GLSurfaceView 有独立 GL 渲染线程——性能好（不和主线程争）、适合全屏视频。TextureView 可以做标准 View 动画（translate/scale/alpha）、适合嵌入场景——但性能可能略差。

---

## 四、音频（3 题）

### Q15：Android 怎么做低延迟音频采集和播放？

**难度/频率**：⭐⭐⭐ / 🔥🔥

> API 26+ 推荐 AAudio（NDK C）——PerformanceMode=LOW_LATENCY，走 FastMixer 通路，延迟 ~5-10ms。如果是 Java 层，AudioRecord(AudioSource.VOICE_COMMUNICATION) 采、AudioTrack.Builder().setPerformanceMode(LOW_LATENCY) 播。VOICE_COMMUNICATION 音频源会让系统启动 Audio DSP 做 AEC/AGC/NS。音频线程不能阻塞/加锁/alloc。数据用无锁环形缓冲解耦。

### Q16：VOICE_COMMUNICATION 和 MIC 音频源有什么区别？

> VOICE_COMMUNICATION：系统启用音频 DSP 做回声消除(AEC)、自动增益(AGC)、降噪(NS)——RTC/直播的标配。MIC：只有裸麦克风数据——录音乐/环境音用这个，RTC 会有严重回声。CAMCORDER：摄像头方向的麦克风。

### Q17：AudioTrack 的 buffer 大小怎么定？太大/太小各有什么问题？

> getMinBufferSize 是硬件最小要求——太小 start 失败或频繁 underrun（爆音）。实际用 2-4 倍。太大 → 延迟高——buffer 装满才开始播，用户感知延时。太小 → CPU 被频繁中断、稍有波动就 underrun。平衡点：实时通信用小 buffer + LOW_LATENCY mode。

---

## 五、JNI 与跨平台（3 题）

### Q18：JNI 怎么把 C++ 引擎接上 Android？

**难度/频率**：⭐⭐⭐ / 🔥🔥

> C++ 推流引擎暴露接口 → JNI 层生成绑定 → Java/Kotlin 层调用。数据传递用 DirectByteBuffer(GetDirectBufferAddress)做零拷贝——避免在 JNI 层 memcpy 每帧 3MB。回调 Java 层：在 JNI_OnLoad 缓存 JavaVM 指针 → 用 cached jclass + jmethodID 做跨线程回调。参考 [[../JNI/00-导读与索引]] 的完整方案。

### Q19：JNI 层的线程要注意什么？

> JNIEnv 是线程绑定的——不能跨线程使用。后台 native 线程回调 Java 前必须 AttachCurrentThread → 拿到该线程的 env → 调 Java 方法 → DetachCurrentThread。JavaVM 是进程级的——在 JNI_OnLoad 缓存，所有线程共享。本地引用有上限(512)——循环里要 DeleteLocalRef。

### Q20：跨平台音视频引擎的 Android 端架构？

> C++ 核心层：音视频同步/码控算法/网络推流/协议协商。Android 桥接层(JNI)：Camera2 → MediaCodec → OpenGL ES，数据统一转成内部 VideoFrame/Packet 进入 C++ 引擎。iOS 桥接层(OC++)：AVCaptureSession → VideoToolbox → Metal。两端上层 API 完全不同但进入 C++ 后是同一种数据结构。比特流格式在桥接层做统一(Android Annex-B 直通、iOS AVCC→Annex-B 转换)。

---

## 六、综合实战（4 题）

### Q21：Android 端推流的完整链路？各环节怎么串？

> 四段。采集：Camera2 + ImageReader(YUV_420_888) → 逐 plane 正确读 YUV。编码：MediaCodec encoder(COLOR_FormatYUV420Flexible, CBR, Baseline) → dequeue/queue 循环 → 输出 Annex-B。推流：Annex-B 直通 C++ RTMP 引擎(不需要格式转换——和 iOS 相反)。音频：AudioRecord(VOICE_COMMUNICATION, 48kHz) → MediaCodec AAC → 同一条 RTMP 通道。线程：CameraThread → EncoderThread → NetworkThread 三级异步 pipeline。停止：先停采集 → flush 编码器 → 断开 RTMP。

### Q22：Android 音视频开发你踩过哪些坑？

> 说三个印象深的。1) 某联发科设备解码 ByteBuffer 模式输出 tiled YUV——CPU 没法直接读，画面整块马赛克。切 Surface 输出立刻修复。2) 华为某机型 YUV_420_888 的 plane[1] pixelStride=4 而不是 2——按 pixelStride=2 拷贝导致色度平面错位、画面偏绿。必须按 getPlanes()[i].getPixelStride() 动态适配。3) AudioTrack 蓝牙 A2DP 输出延迟 200ms+——用户明显感知口型对不上。解决方案：iOS 端蓝牙延迟 50ms 内对比明显，Android 端只能提示用户"有线耳机体验更佳"或 target BLE Audio(LE Audio LC3)。

### Q23：怎么排查 Android 视频处理管线的性能瓶颈？

> Systrace/Perfetto 看各环节耗时：Camera HAL → MediaCodec input → MediaCodec output → GL 渲染。Android Studio CPU Profiler 看 Java 层耗时函数。dumpsys media.codec 看 MediaCodec 内部 buffer 使用量——积压说明某环节慢了。GL 性能：GPUWatch 看每帧 GPU 耗时。内存：Memory Profiler 看 Native 内存是否持续上涨（可能 ByteBuffer/Image 没 release）。

### Q24：Android 4K 直播怎么做性能优化？

> 1) Surface 零拷贝通路——Camera2 → OpenGL ES → MediaCodec(input Surface) 全程 GPU 端，无 CPU 参与。2) AHardwareBuffer 跨 API 共享 GPU 内存。3) 美颜降分辨率处理(720p)再 upsample 到 4K。4) 编码参数：CBR、关 B 帧、短 GOP、Baseline Profile 兼容性优先。5) dma-buf + sync fence 做 GPU↔VPU 流水线并行。详见 [[02-MediaCodec硬编码实战]] / [[04-OpenGLES渲染与Surface详解]] / [[06-端到端采集编码推流管线]]。

---

## 七、自检清单（能流畅回答 20/24 即就绪）

1. MediaCodec 的两条队列模型？同步 vs 异步？
2. 怎么区分硬解软解？
3. 解码花屏三大原因？最快修复？
4. SPS/PPS 在 Android 怎么传？
5. 怎么动态改码率 / 强制关键帧？
6. OMX vs Codec2 的区别？
7. COLOR_FormatYUV420Flexible vs COLOR_FormatSurface？
8. releaseOutputBuffer 第二个参数 true/false？
9. Camera2 怎么拿 YUV？
10. YUV_420_888 三个 plane 怎么正确拷贝？
11. Surface 通路 vs ImageReader 通路？
12. SurfaceTexture 的作用？transform matrix 干什么？
13. OES 纹理 vs GL_TEXTURE_2D 的区别？
14. GLSurfaceView vs TextureView？
15. AAudio vs AudioTrack 延迟差异？
16. VOICE_COMMUNICATION vs MIC 音频源？
17. buffer size 太大/太小的后果？
18. C++ ↔ Java 数据怎么零拷贝？
19. JNI 线程 AttachCurrentThread 的作用？
20. 跨平台引擎 Android 端架构？
21. 完整的 Camera2→编码→RTMP 链路？
22. 踩过的三个坑？
23. 性能排查工具箱？
24. 4K 直播优化策略？

---

## 总结
> Android 音视频中高级面试核心不是 API 背诵，而是碎片化应对能力——颜色格式动态适配、stride/sliceHeight 正确处理、OMX/Codec2 底层理解、Surface 零拷贝链路、Audio 实时约束。MediaCodec 是核心中的核心，颜色格式是最经典的坑，Annex-B 是 Android 对比 iOS 最大的格式差异。

## 关联文档
- [[../ffmpeg/14-Android硬件编解码]] — MediaCodec 专题深挖
- 本系列 00-06 全部文档 — 每个知识点的深入展开
