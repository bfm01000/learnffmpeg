# 音视频 / FFmpeg 学习文档系统重构进度

> 更新时间：2026-08-04 17:49:00

## 0. 执行原则

- 范围：递归扫描项目中与 FFmpeg、音视频、播放器、编解码、封装格式、流媒体、WebRTC、Android/iOS 音视频、零拷贝和示例工程相关的学习文档。
- 排除：第三方库文档、构建产物、算法/设计模式/普通系统编程等与音视频链路无直接关系的资料。
- 状态枚举：`未检查` / `已检查` / `待合并` / `整理中` / `已完成` / `需人工确认`。
- 只要本清单中仍存在 `未检查`、`待合并` 或 `整理中`，就不能宣布全局完成。

## 1. 阶段性结论

- 当前筛选相关文档数：286。
- 已完成：`Doc/ffmpeg/` 主线整理、重复音频指南合并、`100` 核心问答瘦身、`25` 硬解到渲染专题补强。
- 已完成：`Doc/README.md`、`Doc/Android/README.md`、`Doc/iOS/README.md`、`Doc/零拷贝/README.md` 入口建立。
- 已完成：`Doc/Android/`、`Doc/iOS/`、`Doc/零拷贝/` 第二批正文入口定位、demo 索引校准、标题与交叉学习边界整理。
- 已完成：`Doc/cpp/` C++ 音视频工程支撑目录入口定位、Wiki 链接规范化、FFmpeg/播放器工程落点补强。
- 已完成：`Doc/直播/`、`Doc/网络协议/` 入口建立、RTMP 重复边界明确、正文面试/深入定位补齐。
- 下一批建议：整理 WebRTC/项目实战文档、示例工程和面试材料，继续处理重复长文和入口索引。

## 2. 最终目录设计草案

```text
Doc/
├── README.md                         # 全项目音视频学习总入口
├── ffmpeg/                           # FFmpeg 核心：API、编解码、封装、同步、协议、硬件
├── Android/                          # Android 音视频平台专题：Camera2、MediaCodec、OpenGLES、AudioTrack
├── iOS/                              # iOS 音视频平台专题：AVFoundation、VideoToolbox、Metal、AudioUnit
├── 直播/                             # 直播协议专题：RTMP、HTTP-FLV、WebRTC 架构
├── 零拷贝/                           # 跨平台零拷贝/图形缓冲/CPU-GPU 同步专题
└── 自动剪辑/                         # Seek、帧索引、剪辑时间模型专题
project/
├── *_Transcoder*/                    # FFmpeg 转码实战
├── *_Player*/                        # 播放器架构实战
├── *_Seek*/                          # 精准 Seek / 帧索引实战
└── WebRTC/                           # WebRTC 实时音视频专题
av_interview/                         # 面试/JD/项目话术，最终回链到 Doc 主知识体系
learnTarget/                          # 学习路线与职业规划，最终回链到 Doc 主知识体系
```

## 3. 合并策略草案

| 主题 | 权威位置 | 合并策略 | 当前状态 |
|---|---|---|---|
| FFmpeg 核心 API/编解码/时间戳 | `Doc/ffmpeg/` | 已作为主线专题整理；归档重复长文 | 已完成第一轮 |
| 音频 PCM/重采样/工程排查 | `Doc/ffmpeg/04-音频PCM-采样-重采样.md` + `18` + `24` | 原 C++ 音频长指南已合并进 04 并归档 | 已完成 |
| 核心面试问答 | `Doc/ffmpeg/100` + `20` | `100` 保留入口，`20` 保留题库，长讲回专题 | 已完成第一轮 |
| 移动端硬编硬解 | `Doc/Android/`、`Doc/iOS/`、`Doc/ffmpeg/10/14/15/25` | 平台细节留平台目录，跨平台总结留 ffmpeg | 已完成第二轮 |
| 零拷贝 / Surface / IOSurface / AHardwareBuffer | `Doc/零拷贝/` + `Doc/ffmpeg/25` | 已建入口；下一步统一术语和交叉索引 | 已完成第二轮 |
| 直播/流媒体 | `Doc/直播/` + `Doc/ffmpeg/08/12/19/21` | 协议原理留 ffmpeg，直播业务深讲留 `Doc/直播` | 已完成第一轮 |
| 网络协议 | `Doc/网络协议/` | TCP/RTMP/RTP 等底层协议保留在网络协议目录，与直播目录分工 | 已完成第一轮 |
| WebRTC | `project/14_WebRTC_Interview_Project/` + `learnTarget/WebRTC实战指南.md` | 保留项目化学习路径，与 ffmpeg 协议/音频/同步文档互链 | 未检查 |
| C++ 音视频工程支撑 | `Doc/cpp/` | 已统一入口定位和链接；保留为 FFmpeg/播放器工程化底座 | 已完成第一轮 |
| 示例工程说明 | `project/*` | 不随意改代码；文档统一回链到核心专题 | 未检查 |

## 4. 完整清单

| 状态 | 文件路径 | 当前主题 | 内容概述 | 重复/问题 | 是否合并 | 最终归属 |
|---|---|---|---|---|---|---|
| 已完成 | `Doc\Android\_archive\merged-2026-08-04\learning-strategy-question.txt` | Android 音视频 | 如果我只是学习ffmpeg和android，ios编解码，以及linux的知识，让他们生成知识文档，一节回答一些问题，还有就是写一些简单的验证demo，你更推荐哪一个 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\Android\00-Android音视频开发全景导读.md` | Android 音视频 | # Android 音视频开发全景导读 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\Android\01-Camera2采集详解.md` | Android 音视频 | # Camera2 视频采集详解：面试速记与原理详解 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\Android\02-MediaCodec硬编码实战.md` | Android 音视频 | # MediaCodec 硬编码实战：从 YUV 到 H.264 比特流 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\Android\03-MediaCodec硬解码实战.md` | Android 音视频 | # MediaCodec 硬解码实战：从 H.264 比特流到渲染 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\Android\04-OpenGLES渲染与Surface详解.md` | Android 音视频 | # OpenGL ES 渲染与 Surface 详解：从 SurfaceTexture 到屏幕 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\Android\05-AudioTrack与AudioRecord详解.md` | Android 音视频 | # AudioTrack 与 AudioRecord 详解：Android 音频采集与播放 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\Android\06-端到端采集编码推流管线.md` | Android 音视频 | # 端到端采集编码推流管线：从 Camera2 到 RTMP | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\Android\07-Android-OES纹理深入详解.md` | Android 音视频 | # Android OES 纹理深入详解（面试向） | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\Android\99-Android音视频面试题全集.md` | Android 音视频 | # Android 音视频面试题全集：中高级岗位 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\Android\demos\README.md` | Android 音视频 | # Android 音视频硬件编解码 Demo 合集 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\Android\README.md` | Android 音视频 | # Android 音视频学习索引 | 入口已建立 | 目录索引保留 | 目录入口 |
| 已完成 | `Doc\cpp\00-导读与索引.md` | C++ 音视频工程支撑 | # C++ 面试知识库 · 导读与索引 | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\01-多线程与锁.md` | C++ 音视频工程支撑 | # C++ 多线程与锁：面试速记与原理详解 | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\02-原子操作与内存序.md` | C++ 音视频工程支撑 | # C++ 原子操作与内存序（std::atomic / memory_order） | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\03-无锁队列.md` | C++ 音视频工程支撑 | # C++ 无锁队列面试速记与原理详解 | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\04-信号量.md` | C++ 音视频工程支撑 | # C++ 信号量：面试速记与原理详解 | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\08-继承、多态、虚函数与对象模型.md` | C++ 音视频工程支撑 | # C++ 继承、多态与多继承：面试速记与底层原理详解 | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\11-类型转换.md` | C++ 音视频工程支撑 | # C++ 类型转换：四件套与 RTTI | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\14-模板与泛型编程.md` | C++ 音视频工程支撑 | # C++ 模板与泛型编程 | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\17-C++20与协程.md` | C++ 音视频工程支撑 | # C++20 核心特性与协程 | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\21-内存对齐、SIMD与缓冲管理.md` | C++ 音视频工程支撑 | # C++ 内存对齐、SIMD 与缓冲管理 | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\22-自定义分配器与内存池.md` | C++ 音视频工程支撑 | # C++ 自定义分配器、内存池与对象池 | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\23-C与C++互操作及FFmpeg资源封装.md` | C++ 音视频工程支撑 | # C 与 C++ 互操作及 FFmpeg/SDL 资源封装 | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\24-线程池与音视频流水线.md` | C++ 音视频工程支撑 | # C++ 线程池与音视频多线程流水线架构 | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\cpp\25-STL常用操作速查.md` | C++ 音视频工程支撑 | # C++ STL 常用操作速查 | 已完成入口定位/链接规范化 | 保留为 C++ 音视频工程支撑主线 | C++ 工程底座 |
| 已完成 | `Doc\ffmpeg\_archive\avframe_avpacket_guide.md` | FFmpeg 核心专题 | # FFmpeg 新手必读：AVPacket 与 AVFrame 核心指南 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\avsampleformat_interview_guide.md` | FFmpeg 核心专题 | # AVSampleFormat 核心场景与面试指南 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\cpp音视频开发音频问题与面试指南.md` | FFmpeg 核心专题 | # C++ 音视频开发：常见音频问题、核心概念与面试指南 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\ffmpeg_avcodec_vs_context.md` | FFmpeg 核心专题 | # FFmpeg 核心概念：AVCodec 与 AVCodecContext 的关系及初始化流程 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\ffmpeg_cmake_questions.md` | FFmpeg 核心专题 | # FFmpeg 与 CMake 学习问答记录 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\ffmpeg_resource_lifecycle_notes.md` | FFmpeg 核心专题 | # FFmpeg 资源生命周期与常见清理函数笔记 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\h264_mp4_面试速记.md` | FFmpeg 核心专题 | # MP4 <-> H.264 面试速记 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\h264_mp4_模拟面试.md` | FFmpeg 核心专题 | # MP4 <-> H.264 模拟面试（10 连追问） | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\hardware_codec_learning_guide.md` | FFmpeg 核心专题 | # 硬件编解码学习指南 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\merged-2026-08-04\100-核心必会问题.before-merge.md` | FFmpeg 核心专题 | # 100 - 核心必会问题（中高级 C++ 音视频面试） | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\merged-2026-08-04\cpp音视频开发音频问题与面试指南.before-merge.md` | FFmpeg 核心专题 | # C++ 音视频开发：常见音频问题、核心概念与面试指南 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\merged-2026-08-04\cpp音视频开发音频问题与面试指南.md` | FFmpeg 核心专题 | # C++ 音视频开发：常见音频问题、核心概念与面试指南 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\mp4_h264_mock_interview.md` | FFmpeg 核心专题 | # MP4 <-> H.264 模拟面试（10 连追问） | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\mp4-2-ts.md` | FFmpeg 核心专题 | # 从字节视角理解 MP4、AVCC、Annex-B 与 NALU | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\swscontext_lecture.md` | FFmpeg 核心专题 | # SwsContext 讲义 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\码率-Profile.md` | FFmpeg 核心专题 | # H.264 视频压缩核心参数详解：从入门到进阶 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\网络协议通关指南.md` | FFmpeg 核心专题 | # 资深工程师的网络协议通关指南：TCP/UDP 与 HTTP/HTTPS | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\_archive\音视频开发常见问题与解析.md` | FFmpeg 核心专题 | # 音视频开发常见问题与解析 | 归档材料，默认不作为主线 | 不合并到主线，必要时回查 | archive |
| 已完成 | `Doc\ffmpeg\00-FFmpeg全景导读.md` | FFmpeg 核心专题 | # 00 - FFmpeg 全景导读 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\01-数据结构与生命周期.md` | FFmpeg 核心专题 | # 01 - 数据结构与生命周期 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\02-像素格式与内存布局.md` | FFmpeg 核心专题 | # 02 - 像素格式与内存布局 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\03-SwsContext-图像缩放与格式转换.md` | FFmpeg 核心专题 | # 03 - SwsContext：图像缩放与格式转换 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\04-音频PCM-采样-重采样.md` | FFmpeg 核心专题 | # 04 - 音频 PCM、采样格式与重采样 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\05-H264-MP4-NALU.md` | FFmpeg 核心专题 | # 05 - H.264、MP4、NALU 与 AVCC / Annex-B | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\06-编码参数与码控.md` | FFmpeg 核心专题 | # 06 - 编码参数与码控（Profile / Preset / Tune / CRF / CBR / VBR） | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\07-硬件编解码.md` | FFmpeg 核心专题 | # 07 - 硬件编解码（NVENC / VideoToolbox / QSV） | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\08-网络协议与流媒体.md` | FFmpeg 核心专题 | # 08 - 网络协议与流媒体 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\09-题集与自检.md` | FFmpeg 核心专题 | # 09 - 面试题集与自检 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\100-核心必会问题.md` | FFmpeg 核心专题 | # 100 - 核心必会问题（中高级 C++ 音视频面试） | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\101-编解码接口模板对比-FFmpeg-MediaCodec-VideoToolbox-NVIDIA.md` | FFmpeg 核心专题 | # 101 - 编解码接口模板对比：FFmpeg / MediaCodec / VideoToolbox / NVIDIA | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\10-移动端硬件编解码.md` | FFmpeg 核心专题 | # 10 - 移动端硬件编解码（Android MediaCodec / iOS VideoToolbox） | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\11-H264与H265详解.md` | FFmpeg 核心专题 | # 11 - H.264 与 H.265 详解 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\12-RTMP推流详解.md` | FFmpeg 核心专题 | # 12 - RTMP 推流详解 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\13-NVIDIA硬件编解码.md` | FFmpeg 核心专题 | # 13 - NVIDIA 硬件编解码深入（NVENC / NVDEC / CUVID） | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\14-Android硬件编解码.md` | FFmpeg 核心专题 | # 14 - Android 硬件编解码深入（MediaCodec / AMediaCodec） | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\15-iOS硬件编解码.md` | FFmpeg 核心专题 | # 15 - iOS / macOS 硬件编解码深入（VideoToolbox 专题） | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\16-硬件编解码高级专题.md` | FFmpeg 核心专题 | # 16 - 硬件编解码高级专题（面试深水区 · 跨平台共性） | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\17-FFmpeg-Seek详解.md` | FFmpeg 核心专题 | # 17 - FFmpeg Seek 详解：从拖动进度条到帧精确定位 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\18-FFmpeg音频编解码详解.md` | FFmpeg 核心专题 | # 18 - FFmpeg 音频编解码详解（AAC / Opus / MP3） | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\19-HLS详解.md` | FFmpeg 核心专题 | # 19 - HLS 详解（面试导向 + 全景） | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\20-音视频开发面试题库-中高级.md` | FFmpeg 核心专题 | # 20 - 音视频开发面试题库（中高级·全覆盖） | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\21-RTP详解.md` | FFmpeg 核心专题 | # 21 - RTP 详解（面试导向） | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\22-VFR与CFR详解.md` | FFmpeg 核心专题 | # 22 - VFR 与 CFR 详解 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\23-I帧很大P帧很小的问题详解.md` | FFmpeg 核心专题 | # 23 - I 帧很大、P 帧很小：问题原理与解决方案详解 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\24-音视频同步详解.md` | FFmpeg 核心专题 | # 24 - 音视频同步详解 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\25-硬解码到渲染流程-MediaCodec-VideoToolbox.md` | FFmpeg 核心专题 | # 25 - 硬解码到渲染流程：MediaCodec / VideoToolbox | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\99-学习进度.md` | FFmpeg 核心专题 | # FFmpeg 学习 · 进度存档 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 已完成 | `Doc\ffmpeg\README.md` | FFmpeg 核心专题 | # FFmpeg 学习笔记 | 待检查 | 待判断 | FFmpeg 核心主线 |
| 未检查 | `project\1_transcoder\CMakeLists.txt` | FFmpeg 示例工程 | # 定义项目名称为 transcoder，项目语言为 C++ | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\1_transcoder\DESIGN.md` | FFmpeg 示例工程 | # 全能格式转换器（Transcoder）设计文档（阶段1） | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\1_transcoder\ProjectRecorde.txt` | FFmpeg 示例工程 | # 项目目标 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\1_transcoder\README.md` | FFmpeg 示例工程 | # Transcoder (C++ libav*) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\1_transcoder\初学者转码代码讲解.md` | FFmpeg 示例工程 | # `main.cpp` 初学者转码代码讲解 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\10_FrameAccurate_Seek_Demo\CMakeLists.txt` | FFmpeg 示例工程 | cmake_minimum_required(VERSION 3.16) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\10_FrameAccurate_Seek_Demo\逐步讲解.md` | FFmpeg 示例工程 | # Frame-Accurate Seek Demo · 逐步讲解 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\11_FrameIndex_Extraction_Demo\CMakeLists.txt` | FFmpeg 示例工程 | cmake_minimum_required(VERSION 3.16) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\11_FrameIndex_Extraction_Demo\逐步讲解.md` | FFmpeg 示例工程 | # Frame Index Extraction Demo · 逐步讲解 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\12_Hardware_Codec_Demo\CMakeLists.txt` | FFmpeg 示例工程 | # ============================================================ | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\12_Hardware_Codec_Demo\README.md` | FFmpeg 示例工程 | # 12_Hardware_Codec_Demo — 三平台硬件编解码对比演示 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\ARCHITECTURE.md` | FFmpeg 示例工程 | # Linux 高性能播放器 SDK — 架构设计文档 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\CLAUDE.md` | FFmpeg 示例工程 | # CLAUDE.md | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\CMakeLists.txt` | FFmpeg 示例工程 | # ── C++17 ────────────────────────────────────────────────────────────────── | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\control\CMakeLists.txt` | FFmpeg 示例工程 | add_library(control OBJECT | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\core\CMakeLists.txt` | FFmpeg 示例工程 | add_library(core OBJECT | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\decode\CMakeLists.txt` | FFmpeg 示例工程 | # HWAccel compile definitions | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\examples\CMakeLists.txt` | FFmpeg 示例工程 | # ── Simple Player (OpenGL window) ──────────────────────────────────────────── | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\process\CMakeLists.txt` | FFmpeg 示例工程 | add_library(process OBJECT | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\PROGRESS.md` | FFmpeg 示例工程 | # Player SDK — 开发进度追踪 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\README.md` | FFmpeg 示例工程 | # Linux Player SDK | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\REFACTORING.md` | FFmpeg 示例工程 | # Player SDK 架构重构文档 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\render\CMakeLists.txt` | FFmpeg 示例工程 | add_library(render OBJECT | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\source\CMakeLists.txt` | FFmpeg 示例工程 | add_library(source OBJECT | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\target.md` | FFmpeg 示例工程 | 目标： | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\test\CMakeLists.txt` | FFmpeg 示例工程 | # ── GoogleTest ──────────────────────────────────────────────────────────────── | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\utils\CMakeLists.txt` | FFmpeg 示例工程 | add_library(utils OBJECT | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\WORKLOG.md` | FFmpeg 示例工程 | # 工作日志 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\2_video_2_image\CMakeLists.txt` | FFmpeg 示例工程 | cmake_minimum_required(VERSION 3.16) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\5_trancoder_new\CMakeLists.txt` | FFmpeg 示例工程 | cmake_minimum_required(VERSION 3.16) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\5_trancoder_new\README.md` | FFmpeg 示例工程 | 核心步骤 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\6_media_core\CLASS_DESIGN_GUIDE.md` | FFmpeg 示例工程 | # Media Core 类设计详解（面向初学者） | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\6_media_core\CMakeLists.txt` | FFmpeg 示例工程 | cmake_minimum_required(VERSION 3.16) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\6_media_core\README.md` | FFmpeg 示例工程 | # Media Core 设计评审 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\6_media_core\TARGET.md` | FFmpeg 示例工程 | 你是一位资深 C++ 多媒体架构师。请基于 FFmpeg 在我的仓库 `project` 目录下设计并实现一个“可复用的编解码模块（codec core）”，要求后续可直接复用于 WebRTC 工程。并提供使用转码的使用demo | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\8_Simplest_Player\CMakeLists.txt` | FFmpeg 示例工程 | # 生成 compile_commands.json,clangd/Cursor 跳转补全靠它(见 01 §8.3) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\8_Simplest_Player\问题分析.md` | FFmpeg 示例工程 | # Simplest_Player 核心问题与深度技术分析报告 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\8_Simplest_Player\逐步讲解.md` | FFmpeg 示例工程 | # 最简播放器 · 逐步讲解（跟着学） | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\9_Simplest_Transcoder\CMakeLists.txt` | FFmpeg 示例工程 | # 生成 compile_commands.json,clangd/Cursor 跳转补全靠它(见 01 §8.3) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\9_Simplest_Transcoder\逐步讲解.md` | FFmpeg 示例工程 | # 最简转码器 · 逐步讲解（跟着学） | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\filtergraph\Record.txt` | FFmpeg 示例工程 | 我想让ai帮我用ffmpeg完成这样一个这个功能，并且要求先输出文档，文档里面有详细的架构，以及这样设计的原理，并且一个初学者也能看懂原理。之后再根据文档的设计完成代码的实现。请帮我生成一个提示词:功能具体要求如下： | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\自动剪辑-帧ID与时间戳Seek精度方案.md` | FFmpeg 示例工程 | # 自动剪辑：帧ID与时间戳 Seek 精度方案 | 待检查 | 待判断 | 待设计 |
| 已完成 | `Doc\iOS\00-iOS音视频开发全景导读.md` | iOS 音视频 | # iOS 音视频开发全景导读 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\01-AVFoundation采集详解.md` | iOS 音视频 | # AVFoundation 视频采集详解：面试速记与原理详解 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\02-AudioUnit与音频处理详解.md` | iOS 音视频 | # AudioUnit 与 iOS 音频处理：面试速记与原理详解 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\03-GPUImage滤镜链详解.md` | iOS 音视频 | # GPUImage 滤镜链详解：面试速记与原理详解 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\04-Metal渲染与零拷贝详解.md` | iOS 音视频 | # iOS Metal 渲染与零拷贝详解：从 CVPixelBuffer 到屏幕 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\05-VideoToolbox硬编码实战.md` | iOS 音视频 | # VideoToolbox 硬编码实战：从 CVPixelBuffer 到 H.264 比特流 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\06-VideoToolbox硬解码实战.md` | iOS 音视频 | # VideoToolbox 硬解码实战：从 H.264 比特流到 CVPixelBuffer | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\07-AVCC与Annex-B转换实战.md` | iOS 音视频 | # AVCC 与 Annex-B 转换实战：iOS 推流必过的格式关 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\08-端到端采集编码推流管线.md` | iOS 音视频 | # 端到端采集编码推流管线：从 iPhone 摄像头到 RTMP 服务器 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\09-AudioSession与音频策略详解.md` | iOS 音视频 | # AudioSession 与 iOS 音频策略详解：中断、路由、后台 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\10-iOS零拷贝深入详解.md` | iOS 音视频 | # iOS 零拷贝深入详解（面试向） | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\11-IOSurface深入详解.md` | iOS 音视频 | # IOSurface 深入详解（面试向） | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\99-iOS音视频面试题全集.md` | iOS 音视频 | # iOS 音视频开发面试题全集：中高级岗位 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\demos\README.md` | iOS 音视频 | # iOS 音视频硬件编解码 Demo 合集 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\iOS\README.md` | iOS 音视频 | # iOS 音视频学习索引 | 入口已建立 | 目录索引保留 | 目录入口 |
| 未检查 | `learnTarget\WebRTC实战指南.md` | WebRTC/实时音视频 | # WebRTC 从入门到实战：资深音视频工程师的开发指南 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\13_linux_player\docs\WEBRTC_DESIGN.md` | WebRTC/实时音视频 | # WebRTC 播放支持 — 设计方案 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\AGENTS.md` | WebRTC/实时音视频 | # Codex Project Guide | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\architecture.md` | WebRTC/实时音视频 | # 架构说明 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\000-template.md` | WebRTC/实时音视频 | # 000 日志模板 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\001-camera-preview-check.md` | WebRTC/实时音视频 | # 001 摄像头预览验证 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\002-prompt-agents-optimization.md` | WebRTC/实时音视频 | # 002 提示词和 AGENTS 优化 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\003-webrtc-mvp.md` | WebRTC/实时音视频 | # 003 WebRTC MVP | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\004-camera-fallback-test-pattern.md` | WebRTC/实时音视频 | # 004 摄像头不可用时的 Test Pattern Fallback | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\005-stats-dashboard-datachannel.md` | WebRTC/实时音视频 | # 005 Stats Dashboard 和 DataChannel 延迟 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\006-encoding-parameter-control.md` | WebRTC/实时音视频 | # 006 编码参数控制 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\006-stats-json-export.md` | WebRTC/实时音视频 | # 006 Stats JSON Export | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\007-chart-explanation-copy.md` | WebRTC/实时音视频 | # 007 图表中文说明 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\008-full-page-chinese-explanations.md` | WebRTC/实时音视频 | # 008 全页面中文解释 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\009-simple-abr-strategy.md` | WebRTC/实时音视频 | # 009 Simple ABR 策略 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\011-v4l2-capture-demo.md` | WebRTC/实时音视频 | # 011 V4L2 Capture Demo | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\012-ffmpeg-probe-demo.md` | WebRTC/实时音视频 | # 012 FFmpeg Probe Demo | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\013-unified-media-sample.md` | WebRTC/实时音视频 | # 013 统一视频素材 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\014-h264-bitstream-probe.md` | WebRTC/实时音视频 | # 014 H264 Bitstream Probe | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\015-h264-rtp-packetizer.md` | WebRTC/实时音视频 | # Dev Log 015 - H.264 RTP Packetizer | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\016-native-libwebrtc-sender-design.md` | WebRTC/实时音视频 | # Dev Log 016 - Native libwebrtc Sender 设计 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\017-libwebrtc-env-check.md` | WebRTC/实时音视频 | # Dev Log 017 - libwebrtc 环境检查 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\018-libwebrtc-bootstrap-attempt.md` | WebRTC/实时音视频 | # Dev Log 018 - libwebrtc Bootstrap 尝试 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\dev-log\019-wsl-proxy-libwebrtc-blocker.md` | WebRTC/实时音视频 | # Dev Log 019 - WSL 代理阻塞 libwebrtc fetch | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\interview-notes\01-webrtc-call-flow.md` | WebRTC/实时音视频 | # 01 WebRTC 建连流程 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\interview-notes\07-stats-and-datachannel-latency.md` | WebRTC/实时音视频 | # 07 Stats 和 DataChannel 延迟测量 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\interview-notes\08-encoding-parameters-and-abr.md` | WebRTC/实时音视频 | # 08 编码参数控制和 ABR | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\interview-notes\09-v4l2-capture.md` | WebRTC/实时音视频 | # 09 V4L2 摄像头采集 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\interview-notes\10-ffmpeg-timestamp-gop.md` | WebRTC/实时音视频 | # 10 FFmpeg 时间戳和 GOP | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\interview-notes\11-h264-annexb-nalu.md` | WebRTC/实时音视频 | # 11 H264 Annex-B 和 NALU | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\interview-notes\12-h264-rtp-packetizer.md` | WebRTC/实时音视频 | # 面试讲解：H.264 RTP Packetizer | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\interview-notes\13-native-libwebrtc-sender.md` | WebRTC/实时音视频 | # 面试讲解：Native libwebrtc Sender 设计 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\phase3\ffmpeg-timestamp-design.md` | WebRTC/实时音视频 | # FFmpeg Timestamp / GOP Probe 设计说明 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\phase3\h264-bitstream-probe-design.md` | WebRTC/实时音视频 | # H264 Bitstream Probe 设计说明 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\phase3\h264-rtp-packetizer-design.md` | WebRTC/实时音视频 | # H.264 RTP Packetizer 设计说明 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\phase3\libwebrtc-bootstrap-plan.md` | WebRTC/实时音视频 | # libwebrtc Bootstrap 计划 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\phase3\libwebrtc-env-check.md` | WebRTC/实时音视频 | # libwebrtc Native 环境检查报告 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\phase3\native-libwebrtc-sender-design.md` | WebRTC/实时音视频 | # Native libwebrtc Sender 最小 Demo 设计图 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\phase3\native-signaling-protocol.md` | WebRTC/实时音视频 | # Native Sender 信令协议兼容说明 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\phase3\phase3-native-design.md` | WebRTC/实时音视频 | # Phase 3 Native 设计框架 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\phase3\v4l2-capture-design.md` | WebRTC/实时音视频 | # V4L2 Capture 设计说明 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\docs\phase3\wsl-proxy-for-libwebrtc.md` | WebRTC/实时音视频 | # WSL 访问 libwebrtc 依赖源的代理配置 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\native\CMakeLists.txt` | WebRTC/实时音视频 | cmake_minimum_required(VERSION 3.16) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\native\ffmpeg_probe\CMakeLists.txt` | WebRTC/实时音视频 | find_package(PkgConfig REQUIRED) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\native\h264_rtp_packetizer\CMakeLists.txt` | WebRTC/实时音视频 | add_executable(h264_rtp_packetizer | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\native\libwebrtc_sender\README.md` | WebRTC/实时音视频 | # Native libwebrtc Sender | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\native\README.md` | WebRTC/实时音视频 | # Native Modules | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\native\v4l2_capture\CMakeLists.txt` | WebRTC/实时音视频 | add_executable(v4l2_capture | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\PROMPT.md` | WebRTC/实时音视频 | # LowLatency WebRTC Lab 项目提示词 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\14_WebRTC_Interview_Project\README.md` | WebRTC/实时音视频 | # LowLatency WebRTC Lab | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\00-README.md` | WebRTC/实时音视频 | # WebRTC 学习项目 · 索引 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\01-入门导读.md` | WebRTC/实时音视频 | # WebRTC 入门导读：从零理解实时通信 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\02-架构总览.md` | WebRTC/实时音视频 | # 阶段一：WebRTC 整体架构总览 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\03-音视频基础self-check.md` | WebRTC/实时音视频 | # 阶段零：C++ 音视频基础 self-check 清单 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\04-项目设计方案.md` | WebRTC/实时音视频 | # 阶段二：项目设计方案（Top-down Design） | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\05-环境准备清单-macOS.md` | WebRTC/实时音视频 | # macOS 环境准备清单（WebRTC 项目） | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\06-M4-RTP传输模块.md` | WebRTC/实时音视频 | # 阶段三 · M4：RTP 传输模块（B 层重写核心模块 1/2） | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\06-M4-RTP传输模块\code\CMakeLists.txt` | WebRTC/实时音视频 | cmake_minimum_required(VERSION 3.14) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\06-M4-RTP传输模块\code\README.md` | WebRTC/实时音视频 | # M4 · RTP 传输模块 · B 层代码 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\06-M4-RTP传输模块\code\tests\CMakeLists.txt` | WebRTC/实时音视频 | include(FetchContent) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\07-M6-JitterBuffer模块.md` | WebRTC/实时音视频 | # 阶段三 · M6：视频 Jitter Buffer（B 层重写核心模块 2/2） | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\07-M6-JitterBuffer模块\code\CMakeLists.txt` | WebRTC/实时音视频 | cmake_minimum_required(VERSION 3.14) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\07-M6-JitterBuffer模块\code\README.md` | WebRTC/实时音视频 | # M6 · 视频 Jitter Buffer · B 层代码 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\07-M6-JitterBuffer模块\code\tests\CMakeLists.txt` | WebRTC/实时音视频 | include(FetchContent) | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\08-SDP真实样本解析.md` | WebRTC/实时音视频 | # 读懂一段真实 SDP：从浏览器 step-02 抓出的生产级样本 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\09-面试讲述.md` | WebRTC/实时音视频 | # 阶段四：项目面试讲述与包装 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\10-NTP与RTCP-SR详解.md` | WebRTC/实时音视频 | # 10 - NTP 与 RTCP SR：RTP 时间戳怎么和真实时钟挂钩 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\11-WebRTC-QoS四驾马车-GCC-FEC-NACK-JitterBuffer.md` | WebRTC/实时音视频 | # WebRTC QoS 四驾马车：GCC · FEC · NACK · JitterBuffer | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\99-面试题全集-WebRTC.md` | WebRTC/实时音视频 | # WebRTC 面试题全集 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\99-面试题全集-音视频基础.md` | WebRTC/实时音视频 | # 音视频基础面试题全集 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\99-项目进度存档.md` | WebRTC/实时音视频 | # WebRTC 学习项目 · 进度存档 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\99-学习提示词.md` | WebRTC/实时音视频 | # WebRTC 架构自顶向下学习（面向 C++ 音视频面试） | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\WebRTC入门导读.md` | WebRTC/实时音视频 | # WebRTC 入门导读：从零理解实时通信 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\阶段三-M4-RTP传输模块.md` | WebRTC/实时音视频 | 待阅读完整内容后补充 | 待检查 | 待判断 | 待设计 |
| 未检查 | `project\WebRTC\名词详解.md` | WebRTC/实时音视频 | # SSRC: | 待检查 | 待判断 | 待设计 |
| 未检查 | `.claude\skills\learn-with-map.md` | 待细分 | # Skill: 全景式面试学习资料生成器（learn-with-map） | 待检查 | 待判断 | 待设计 |
| 未检查 | `CMakeLists.txt` | 待细分 | cmake_minimum_required(VERSION 3.16) | 待检查 | 待判断 | 待设计 |
| 未检查 | `Doc\OC\【重点】OC_Category分类详解.md` | 待细分 | # Objective-C Category (分类) 从零到精通：原理与面试速记 | 待检查 | 待判断 | 待设计 |
| 未检查 | `Doc\OC\【重点】OC_Runtime.md` | 待细分 | # Objective-C Runtime 核心原理、避坑与面试指南 | 待检查 | 待判断 | 待设计 |
| 未检查 | `Doc\OC\【重点】OC面试高频考点与标准回答大全.md` | 待细分 | # OC (Objective-C) 高频面试考点与标准回答（口语化版） | 待检查 | 待判断 | 待设计 |
| 已完成 | `Doc\网络协议\RTMP协议深度解析与面试指南.md` | 待细分 | # RTMP 协议深度解析与面试指南：从会用推流到能说“精通 RTMP” | 已完成入口定位/职责边界整理 | 保留主线，RTMP 重复以职责分工解决 | 直播/网络协议主线 |
| 已完成 | `Doc\网络协议\TCP协议深度解析与面试指南.md` | 待细分 | # TCP 协议深度解析与面试指南：从三次握手到弱网下的音视频传输 | 已完成入口定位/职责边界整理 | 保留主线，RTMP 重复以职责分工解决 | 直播/网络协议主线 |
| 已完成 | `Doc\网络协议\音视频网络协议面试指南.md` | 待细分 | # 音视频 C++ 网络协议面试指南：从 Muduo 封装到实时传输底层能力 | 已完成入口定位/职责边界整理 | 保留主线，RTMP 重复以职责分工解决 | 直播/网络协议主线 |
| 未检查 | `Doc\渲染\GPU单帧渲染时间统计.md` | 待细分 | # 如何正确统计 GPU 单帧渲染时间：从 glFinish 到 Timer Query 的性能演进 | 待检查 | 待判断 | 待设计 |
| 未检查 | `Doc\预览延时\VideoQueue3_AJB_分析.md` | 待细分 | # QueueSampleGroup3 面试复习版（AJB 实时预览） | 待检查 | 待判断 | 待设计 |
| 未检查 | `docs_reorganization_progress.md` | 待细分 | # 音视频 / FFmpeg 学习文档系统重构进度 | 待检查 | 待判断 | 待设计 |
| 未检查 | `todo.md` | 待细分 | 面试前要做的事情： | 待检查 | 待判断 | 待设计 |
| 已完成 | `Doc\零拷贝\AHardwareBuffer_从浅入深完全解析.md` | 零拷贝/图形缓冲 | # AHardwareBuffer 从浅入深完全解析：Android 零拷贝与图形底层的“终极密码” | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\零拷贝\Android_4K_Live_知识点深度复习.md` | 零拷贝/图形缓冲 | # Android 4K 全景直播：底层渲染与音视频硬核知识点深度复习 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\零拷贝\Android图形渲染.md` | 零拷贝/图形缓冲 | # Android 图形渲染与 BMGMedia SDK 架构复习指南 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\零拷贝\Android硬件编解码.md` | 零拷贝/图形缓冲 | # 硬件解码与 AVFrame 理解指南 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\零拷贝\CPU_GPU读写图像特性.md` | 零拷贝/图形缓冲 | # 图像格式与内存排布：为什么同一块内存对 CPU/GPU 的“友好程度”不同 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\零拷贝\GPU同步.md` | 零拷贝/图形缓冲 | # GPU同步策略说明：`glFinish` vs `glFenceSync + glWaitSync` | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\零拷贝\oryol简介.md` | 零拷贝/图形缓冲 | # 深入透析跨平台渲染引擎 Oryol：RHI 架构、踩坑指南与项目实战 | 已完成入口定位/排版边界整理 | 保留主线，必要重复以交叉链接解决 | 移动端/零拷贝主线 |
| 已完成 | `Doc\零拷贝\README.md` | 零拷贝/图形缓冲 | # 零拷贝与图形缓冲学习索引 | 入口已建立 | 目录索引保留 | 目录入口 |
| 已完成 | `Doc\README.md` | 全局音视频索引 | # 音视频学习文档总索引 | 待检查 | 待判断 | 目录入口 |
| 未检查 | `learnTarget\FFmpeg还有竞争力吗与学习策略总结.md` | 学习路线/职业规划 | # FFmpeg 还有竞争力吗与学习策略总结 | 待检查 | 待判断 | 待设计 |
| 未检查 | `learnTarget\FFmpeg核心基础与理论讲义.md` | 学习路线/职业规划 | # FFmpeg 核心基础与底层理论讲义（C++ 视角） | 待检查 | 待判断 | 待设计 |
| 未检查 | `learnTarget\scripts\cursor_monetization.md` | 学习路线/职业规划 | # 拥有 Cursor 后的变现指南：从工具到生产力 | 待检查 | 待判断 | 待设计 |
| 未检查 | `learnTarget\scripts\methodology_examples.md` | 学习路线/职业规划 | # 顶级高手与普通优秀者的“三观与方法论”图鉴 | 待检查 | 待判断 | 待设计 |
| 未检查 | `learnTarget\scripts\practical_methodologies.md` | 学习路线/职业规划 | # 程序员实用方法论与面试绝杀指南 | 待检查 | 待判断 | 待设计 |
| 未检查 | `learnTarget\下一步行动计划.md` | 学习路线/职业规划 | # 下一步行动计划 | 待检查 | 待判断 | 待设计 |
| 未检查 | `learnTarget\项目.md` | 学习路线/职业规划 | ### 0.将图片转成对应的YUV图片，指定前N帧 | 待检查 | 待判断 | 待设计 |
| 未检查 | `learnTarget\学习目标.md` | 学习路线/职业规划 | 你现在是一位资深的音视频技术专家和职业规划导师。我是一名拥有4年9个月经验的移动端C++开发工程师，希望转型并深入学习音视频开发，目标是在一段时间内具备应聘【中高级音视频开发工程师】的能力。帮我输出一片文档到根目录下来完成这个任务。 | 待检查 | 待判断 | 待设计 |
| 未检查 | `learnTarget\音视频中高级开发学习与职业规划.md` | 学习路线/职业规划 | # 音视频中高级开发工程师学习与职业规划指南（FFmpeg 核心聚焦版） | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\Diary\2026-05-05.md` | 音视频面试/JD/项目话术 | 今天是五一假日的最后一天，我一个人在家，总是感觉自己在家无法学习，在学习的时候总会想着去干别的视频，看短视频，撸猫，看片。 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\interview\Handle-桌面端音视频编辑开发面试题.md` | 音视频面试/JD/项目话术 | # 桌面端音视频编辑研发工程师（C/C++）模拟面试题 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\interview\iOS_AI剪辑岗位_项目经历追问与回答策略.md` | 音视频面试/JD/项目话术 | # iOS AI剪辑岗位：项目经历追问与回答策略 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\interview\iOS研发工程师_AI剪辑_剪映CapCut模拟面试.md` | 音视频面试/JD/项目话术 | # 模拟面试：iOS研发工程师（AI剪辑）- 剪映CapCut（深圳/广州） | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\interview\面试必答问题.md` | 音视频面试/JD/项目话术 | # 面试必答问题 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\interview\中高级CPP开发工程师模拟面试.md` | 音视频面试/JD/项目话术 | # 中高级 C++ 开发工程师模拟面试题（附标准回答） | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\interview\桌面端面试文档\CPP锁机制与音视频实战.md` | 音视频面试/JD/项目话术 | # C++ 锁机制与音视频实战场景解析 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\interview\桌面端面试文档\并发控制与无锁队列.md` | 音视频面试/JD/项目话术 | # 音视频并发控制：无锁队列与流控策略 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\interview\桌面端面试文档\时间线区间合并算法.md` | 音视频面试/JD/项目话术 | # 音视频编辑算法：时间线区间合并 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\linux-test1.md` | 音视频面试/JD/项目话术 | 1、负责根据产品需求和定义，按时完成设计和开发测试工作； | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\QW.md` | 音视频面试/JD/项目话术 | # 阿里 · 千问/夸克 · 浏览器/渲染/PDF 内核岗 — 岗位匹配分析 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\QW-面试准备-一周计划\Day01-C++内存与对象模型.md` | 音视频面试/JD/项目话术 | # Day 1 · C++ 内存与对象模型 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\QW-面试准备-一周计划\Day02-C++进阶与常见坑.md` | 音视频面试/JD/项目话术 | # Day 2 · C++ 进阶与常见坑 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\QW-面试准备-一周计划\Day03-多线程与并发实战.md` | 音视频面试/JD/项目话术 | # Day 3 · 多线程与并发实战 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\QW-面试准备-一周计划\Day06-浏览器架构与设计模式.md` | 音视频面试/JD/项目话术 | # Day 6 · 浏览器架构与设计模式 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\QW-面试准备-一周计划\Day07-模拟面试与复盘.md` | 音视频面试/JD/项目话术 | # Day 7 · 模拟面试与周复盘 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\QW-面试准备-一周计划\周复盘清单.md` | 音视频面试/JD/项目话术 | # 周复盘清单 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\QW-面试准备-一周计划\资源与本地文档索引.md` | 音视频面试/JD/项目话术 | # 资源与本地文档索引 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\QW-面试准备-一周计划\自我介绍与项目话术模板.md` | 音视频面试/JD/项目话术 | # 自我介绍与项目话术模板 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\XHS.md` | 音视频面试/JD/项目话术 | 工作职责: | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\XHS2.md` | 音视频面试/JD/项目话术 | 工作职责 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\雷鸟.md` | 音视频面试/JD/项目话术 | # 模拟面试问题（附标准回答） | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\JD\面试经历.md` | 音视频面试/JD/项目话术 | * 字节三面挂了(总共三面技术面试) | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\self-talk.md` | 音视频面试/JD/项目话术 | # 离职原因（推荐版） | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\面试记录.md` | 音视频面试/JD/项目话术 | # 面试核心问题 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\KMP-Json配置化重构.md\AI驱动的大规模KMP-SDK重构实践-技术白皮书.md` | 音视频面试/JD/项目话术 | # AI 驱动的大规模 KMP SDK 重构实践 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\KMP-Json配置化重构.md\CameraParam-方案比较与取舍.md` | 音视频面试/JD/项目话术 | # CameraParam 参数抽象：方案比较与取舍 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\KMP-Json配置化重构.md\README.md` | 音视频面试/JD/项目话术 | # KMP SDK 重构项目文档 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\KMP-Json配置化重构.md\已知不足与后续改进.md` | 音视频面试/JD/项目话术 | # 已知不足与后续改进 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\KMP-Json配置化重构.md\预备-KMP-Json配置化重构.md` | 音视频面试/JD/项目话术 | # Vertical Industry Camera SDK 重构 · 面试讲述文档 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\KMP-Json配置化重构.md\自述.md` | 音视频面试/JD/项目话术 | 我们的旧版本的SDK首先存在以下问题 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\待完成\AI简历\self.md` | 音视频面试/JD/项目话术 | ### 🎓 教育经历 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\待完成\ios零拷贝优化分享.md` | 音视频面试/JD/项目话术 | # EffectHandle 输出路径零拷贝优化 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\待完成\Mat内存复用方案.md` | 音视频面试/JD/项目话术 | # cv::Mat 内存复用方案 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\待完成\大厂常见优化与小厂易踩坑-音视频SDK.md` | 音视频面试/JD/项目话术 | # 大厂常见优化 vs 小厂易踩坑（音视频 SDK 视角） | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\待完成\自定义surface优化.md` | 音视频面试/JD/项目话术 | # 🏆 面试通关秘籍：Android 4K 全景直播极限性能优化实录 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\待完成\自动剪辑-帧ID与时间戳Seek精度方案.md` | 音视频面试/JD/项目话术 | # 自动剪辑：时间戳转换导致 Seek 帧不准 —— 完整解决方案与 FFmpeg 流程详解 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\核心-预览延时优化.md` | 音视频面试/JD/项目话术 | # 车载相机预览低时延优化：项目复盘与面试指南 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\核心-帧索引精度优化.md` | 音视频面试/JD/项目话术 | # 面试实战：抽帧 Seek 精度与帧索引系统重构 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\核心-直播性能优化.md` | 音视频面试/JD/项目话术 | # 面试实战：Android 4K 全景直播全链路性能优化与重构 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\核心-自动剪辑性能优化.md` | 音视频面试/JD/项目话术 | # 自动剪辑性能优化项目 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\面试官追问_预览延时优化.md` | 音视频面试/JD/项目话术 | # 车载相机预览低时延优化：面试官连环追问实战 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\面试官追问_帧索引精度优化.md` | 音视频面试/JD/项目话术 | # 帧索引精度优化项目：面试官连环追问实战 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\面试官追问_直播性能优化.md` | 音视频面试/JD/项目话术 | # 面试官视角：Android 4K 全景直播性能优化项目深度连环追问 | 待检查 | 待判断 | 待设计 |
| 未检查 | `av_interview\项目\面试官追问_自动剪辑性能优化.md` | 音视频面试/JD/项目话术 | # 自动剪辑性能优化项目：面试官连环追问实战 | 待检查 | 待判断 | 待设计 |
| 已完成 | `Doc\直播\FLV与HTTP-FLV直播封装深入理解.md` | 直播/流媒体 | # FLV 与 HTTP-FLV 直播封装深入理解 | 已完成入口定位/职责边界整理 | 保留主线，RTMP 重复以职责分工解决 | 直播/网络协议主线 |
| 已完成 | `Doc\直播\RTMP直播协议深入理解与面试指南.md` | 直播/流媒体 | # RTMP 直播协议深入理解与面试指南 | 已完成入口定位/职责边界整理 | 保留主线，RTMP 重复以职责分工解决 | 直播/网络协议主线 |
| 已完成 | `Doc\直播\WebRTC从浅入深：实时音视频架构与面试指南.md` | 直播/流媒体 | # WebRTC 从浅入深：实时音视频架构与面试指南 | 已完成入口定位/职责边界整理 | 保留主线，RTMP 重复以职责分工解决 | 直播/网络协议主线 |
| 未检查 | `Doc\自动剪辑\seek优化.md` | 自动剪辑/Seek/时间戳 | # FrameReaderInternal Seek 优化设计文档 | 待检查 | 待判断 | 待设计 |
| 未检查 | `Doc\自动剪辑\自动剪辑优化.md` | 自动剪辑/Seek/时间戳 | # 自动剪辑项目经历深度优化稿 | 待检查 | 待判断 | 待设计 |

## 5. 批次日志

### 2026-08-04 16:47:02

- 已重建进度表格，修复路径 Markdown 格式问题。
- 已读取用户重构规格、项目索引和相关子项目规范。
- 已完成初次递归扫描和相关文档筛选。
- 已完成 `Doc/ffmpeg` 第一轮合并整理。
- 已建立 `Doc/README.md`、`Doc/Android/README.md`、`Doc/iOS/README.md`、`Doc/零拷贝/README.md`。
- 已将 `Doc/Android/Untitled` 归档为 `Doc/Android/_archive/merged-2026-08-04/learning-strategy-question.txt`。
- 仍需继续处理直播、WebRTC、示例工程、学习路线和面试材料。
