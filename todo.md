面试前要做的事情：
1. WebRTC重写：
    * jitter Buffer
    * RTP

2. WebRTC整个流程梳理清楚 
    * 拥塞控制可以讲清楚

3. 自动剪辑seek拓展

4. 项目补充
* 详细讲一下你是怎么引入零拷贝的，梳理整个流程通路
* 

5. Android / iOS 硬件编解码（移动端的核心壁垒）

性能的生死线：在移动端，单纯靠 C++ 软解 H.264/H.265 会导致手机发烫、掉帧、耗电极快。因此，必须使用硬解码。

技术要点：

Android：深入掌握 MediaCodec 的同步/异步模式，如何与 Surface / OpenGL ES 结合（即利用 GPU 的纹理 OES 进行零拷贝渲染或前处理）。

iOS：深入掌握 VideoToolbox（VTCompressionSession / VTDecompressionSession）和 AudioToolbox，以及 CVPixelBuffer 的内存管理。

C++ 的结合点：虽然 Android 和 iOS 的硬编解码多有系统原生的 API，但现代音视频架构（如底层跨平台 SDK）都是用 C++ 承载核心骨架，再通过 JNI (Android) 或 Objective-C++ (iOS) 去调用系统底层的硬编解码。 这种跨平台封装能力非常值钱。

其他：

* 码率切换导致的画面模糊应该如何处理
* 去看一下预览延时优化部分，h264解码出来的帧格式是什么

* ===
bt.601
bt.709
flv

* ffmpeg常用接口都列出来，附上在什么场景或者整个pipeline的哪个流程中会用到。以及使用场景和方法

* 学习抓trace

* Android 踩坑：MediaCodec 硬件编解码的“绿边/花屏”与碎片化
既然你做了 Android 4K 直播，面试官极大概率会问你 Android 硬件编解码的坑。

现象：同一套代码，在 Pixel 上正常，在某些国产机（如小米、OV、华为的特定低端机型）上解码出来的画面底部或右侧有绿边，或者画面倾斜、花屏。
根本原因：内存对齐（Alignment）与 Stride（跨距）问题。
很多硬件解码器要求图像的宽高必须是 16 或 32 的整数倍。比如你送进去一个 1080 x 1920 的视频，解码器内部可能会把它 padding（填充）成 1088 x 1920，多出来的 8 个像素如果没有正确处理，就会显示成默认的绿色（YUV 格式下 Y=0, U=0, V=0 刚好是绿色）。
解决方案：
正确解析 Crop 信息：不能死板地用配置的宽高。必须从 MediaCodec 的输出 MediaFormat 中读取 crop-left, crop-top, crop-right, crop-bottom 这四个字段，计算出真实的有效画面区域，然后在 OpenGL 渲染时通过修改纹理坐标（Texture Coordinates）把绿边裁剪掉。
软硬解兜底策略（Fallback）：捕获 MediaCodec 的 MediaCodec.CodecException，一旦发现硬件解码器初始化失败或抛出严重异常，立刻无缝降级切换到 FFmpeg 软解（CPU 解码），保证业务可用性。

