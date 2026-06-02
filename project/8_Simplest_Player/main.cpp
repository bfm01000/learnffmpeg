// 最简播放器：demux MP4 → decode → 视频(swscale→SDL 画面) + 音频(swr→SDL 出声)
// 对应 Doc/ffmpeg/99-学习进度.md 优先级 1/3。
// 📖 逐步原理讲解(配套复习文档)：见同目录 逐步讲解.md
//
// 视频框架(7 步,已完成):
//   ① 解封装：打开文件、找视频流
//   ② 读视频流信息(宽高/编码)
//   ③ 建解码器(AVCodec + AVCodecContext)
//   ④ 解码循环(send_packet / receive_frame)
//   ⑤ YUV → RGB(SwsContext)
//   ⑥ SDL 开窗、把 RGB 画上去
//   ⑦ 按 pts 控制播放节奏 + drain + 清理
//
// 音频续作(A1~A4,进行中):
//   A1 找音频流 + 建音频解码器(镜像②③)                ← 本步已完成
//   A2 解码音频 + swr 重采样成 S16 交错立体声
//   A3 SDL 打开音频设备 + 播放 PCM
//   A4 音频时钟作主时钟,视频追音频(重写⑦ + 补丢帧)
//
// 用法: ./simplest_player <视频文件.mp4>

extern "C" {
// FFmpeg 是 C 库,在 C++ 里必须用 extern "C" 包,否则链接时找不到符号(见 01 §8.1)
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>   // av_get_pix_fmt_name：把像素格式枚举转成可读名("yuv420p")
#include <libavutil/imgutils.h>  // av_image_alloc：按宽高/格式分配一块对齐好的图像缓冲
#include <libavutil/samplefmt.h> // av_get_sample_fmt_name / av_get_bytes_per_sample
#include <libavutil/mathematics.h>  // av_rescale_rnd：按采样率比例算输出样本数
#include <libswscale/swscale.h>  // SwsContext：缩放 + 像素格式转换(YUV→RGB)
#include <libswresample/swresample.h>  // SwrContext：音频重采样(采样率/声道/采样格式转换)
}

#include <SDL.h>  // SDL2：开窗、把 RGB 上传成纹理画到屏幕(C 库,但自带 extern "C",无需再包)

#include <cstdint>
#include <cstdio>

// 建解码器并打开——视频③已逐行讲过这四步(find_decoder → alloc_context3 →
// parameters_to_context → open2)。音频是一模一样的流程,所以抽成函数复用:
// 第一次(视频③)展开全讲,第二次(音频 A1)就该抽象掉,别复制粘贴。
// 失败时把已分配的上下文 free 掉再返回 nullptr,调用方只需判空。
static AVCodecContext *openDecoder(AVStream *stream) {
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) return nullptr;
    AVCodecContext *context = avcodec_alloc_context3(decoder);
    if (!context) return nullptr;
    if (avcodec_parameters_to_context(context, stream->codecpar) < 0 ||
        avcodec_open2(context, decoder, nullptr) < 0) {
        avcodec_free_context(&context);
        return nullptr;
    }
    return context;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法: %s <视频文件.mp4>\n", argv[0]);
        return 1;
    }
    const char *inputPath = argv[1];

    // ===== 阶段①：解封装(demux)——打开文件 =====
    // AVFormatContext 是解封装的"总上下文",统领文件里的各路流(见 01 §一)。
    // 先置 nullptr：avformat_open_input 会负责分配它,出错时也会清理。
    AVFormatContext *formatContext = nullptr;
    if (avformat_open_input(&formatContext, inputPath, nullptr, nullptr) < 0) {
        std::fprintf(stderr, "打不开文件: %s\n", inputPath);
        return 1;
    }
    std::printf("✅ 打开成功: %s\n", inputPath);

    // ===== 阶段②：读视频流信息——找出视频流、读宽高和编码 =====
    // 有些容器(尤其网络流)开头读不到完整信息,要先探测一段数据才知道有几路流、什么编码。
    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        std::fprintf(stderr, "读不到流信息\n");
        avformat_close_input(&formatContext);
        return 1;
    }

    // 一个文件可能有多路流(视频/音频/字幕)。av_find_best_stream 直接帮我们挑出"最佳视频流",
    // 返回它在 formatContext->streams[] 里的下标(找不到返回负数)。
    int videoStreamIndex =
        av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) {
        std::fprintf(stderr, "文件里没有视频流\n");
        avformat_close_input(&formatContext);
        return 1;
    }
    AVStream *videoStream = formatContext->streams[videoStreamIndex];

    // codecpar(AVCodecParameters)是"流的参数描述":宽高、像素格式、编码 id、extradata(SPS/PPS) 等。
    // 注意它只是"描述",真正解码要用它去建解码器(下一步)。
    AVCodecParameters *codecParameters = videoStream->codecpar;
    std::printf("视频流 #%d, %dx%d, 编码=%s\n", videoStreamIndex,
                codecParameters->width, codecParameters->height,
                avcodec_get_name(codecParameters->codec_id));

    // ===== 阶段③：建解码器(AVCodec 算法本体 + AVCodecContext 工作环境)=====
    // 这正是 01 §4 的核心：codecpar 只是"描述",要解码得建一个有状态的解码器实例。
    // (1) 按编码 id 找到对应解码器(h264 → h264 解码器)。AVCodec 是无状态的算法本体,全局共享。
    const AVCodec *decoder = avcodec_find_decoder(codecParameters->codec_id);
    if (!decoder) {
        std::fprintf(stderr, "找不到对应解码器\n");
        avformat_close_input(&formatContext);
        return 1;
    }
    // (2) 分配解码器上下文(AVCodecContext,有状态:存宽高、像素格式、内部参考帧缓冲等)。
    AVCodecContext *codecContext = avcodec_alloc_context3(decoder);
    if (!codecContext) {
        std::fprintf(stderr, "分配解码器上下文失败\n");
        avformat_close_input(&formatContext);
        return 1;
    }
    // (3) 把流的参数(宽高/像素格式/extradata 即 SPS-PPS)从 codecpar 拷进上下文。
    if (avcodec_parameters_to_context(codecContext, codecParameters) < 0) {
        std::fprintf(stderr, "拷贝解码参数失败\n");
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return 1;
    }
    // (4) 启动底层解码引擎。这就是 01 §4.3 "初始化三步" 的最后一步。
    if (avcodec_open2(codecContext, decoder, nullptr) < 0) {
        std::fprintf(stderr, "打开解码器失败\n");
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return 1;
    }
    std::printf("✅ 解码器就绪: %s\n", decoder->name);

    // ===== A1：找音频流 + 建音频解码器(可选——没有音频也能只播视频)=====
    // 和②找视频流同理,这次找 AUDIO。音频可能不存在(纯视频文件),所以是"有就接、没有就跳过"。
    int audioStreamIndex =
        av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    AVStream *audioStream = nullptr;
    AVCodecContext *audioCodecContext = nullptr;
    if (audioStreamIndex >= 0) {
        audioStream = formatContext->streams[audioStreamIndex];
        audioCodecContext = openDecoder(audioStream);  // 和视频③同样的四步,复用函数
        if (audioCodecContext) {
            // ch_layout 是新版声道布局 API(旧的 ->channels 已废弃);采样格式这里通常是 fltp(planar float)。
            std::printf("音频流 #%d, %d Hz, %d 声道, 采样格式=%s, 编码=%s\n",
                        audioStreamIndex, audioCodecContext->sample_rate,
                        audioCodecContext->ch_layout.nb_channels,
                        av_get_sample_fmt_name(audioCodecContext->sample_fmt),
                        avcodec_get_name(audioCodecContext->codec_id));
        }
    } else {
        std::printf("(没有音频流,只播视频)\n");
    }

    // ===== A2 准备：建音频重采样器(swr)=====
    // 解码器吐的音频通常是 fltp(planar float,每声道一个平面),但声卡要的是
    // S16 交错(packed,左右声道样本交替排)。swr 就负责这层转换(采样率/声道/格式)。见 04 §重采样。
    SwrContext *swrContext = nullptr;
    const int outSampleRate = 48000;
    const AVSampleFormat outSampleFormat = AV_SAMPLE_FMT_S16;     // 16 位有符号整数,SDL 最常用
    AVChannelLayout outChannelLayout = AV_CHANNEL_LAYOUT_STEREO;  // 统一输出立体声(单声道源会被复制成两声道)
    const int outChannels = outChannelLayout.nb_channels;
    const int outBytesPerSample = av_get_bytes_per_sample(outSampleFormat);
    // A4 要用:每秒音频字节数 = 采样率 × 声道 × 每样本字节,用来把"已播放字节"换算成"已播放秒数"
    const int audioBytesPerSecond = outSampleRate * outChannels * outBytesPerSample;
    if (audioCodecContext) {
        // 目标参数在前、源参数在后。swr 会把源(fltp/48k/单声道)转成目标(S16/48k/立体声)。
        swr_alloc_set_opts2(&swrContext,
                            &outChannelLayout, outSampleFormat, outSampleRate,
                            &audioCodecContext->ch_layout, audioCodecContext->sample_fmt,
                            audioCodecContext->sample_rate, 0, nullptr);
        swr_init(swrContext);
    }

    // ===== 阶段⑥ 准备：初始化 SDL 视频 + 音频子系统 =====
    // 这里只 Init 子系统(不需要宽高);窗口/渲染器/纹理等拿到首帧知道真实尺寸后再懒创建,
    // 和⑤的 SwsContext 一个套路——用 frame 的确凿尺寸,比提前用 codecpar 更稳。
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::fprintf(stderr, "SDL 初始化失败: %s\n", SDL_GetError());
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return 1;
    }

    // ===== A3：打开声卡。用 SDL_QueueAudio 方式(不写回调),把 PCM 排队喂进去最简单 =====
    SDL_AudioDeviceID audioDevice = 0;
    uint64_t totalQueuedBytes = 0;  // 累计排进声卡的字节数(A4 算音频时钟用)
    if (audioCodecContext) {
        SDL_AudioSpec wantedSpec;
        SDL_zero(wantedSpec);
        wantedSpec.freq = outSampleRate;
        wantedSpec.format = AUDIO_S16SYS;    // 对应⑤⑥那种"格式必须对上":S16 + 本机字节序
        wantedSpec.channels = (Uint8)outChannels;
        wantedSpec.samples = 1024;           // 声卡每次回调要的样本数(缓冲粒度)
        wantedSpec.callback = nullptr;       // nullptr = 用 SDL_QueueAudio 推送,不手写回调
        audioDevice = SDL_OpenAudioDevice(nullptr, 0, &wantedSpec, nullptr, 0);
        if (audioDevice == 0) {
            std::fprintf(stderr, "打不开声卡(将静音播放): %s\n", SDL_GetError());
        } else {
            SDL_PauseAudioDevice(audioDevice, 0);  // 0 = 取消暂停,开始播放
        }
    }

    // ===== 阶段④：解码循环(send_packet / receive_frame 异步状态机,见 01 §6.4)=====
    // 核心心智模型：发包和取帧是"解耦"的两条管道,不是 1 进 1 出。
    //   - 喂一个 packet 进去,可能一帧都取不出来(B 帧要等后续参考帧,解码器先攒着)；
    //   - 也可能一次取出好几帧。所以是"喂一个包 → 把当前能取的帧全取干净"的双层循环。
    // packet 装"压缩数据"(一个 NALU 序列),frame 装"解码后的原始像素"(这里是 YUV420P)。
    AVPacket *packet = av_packet_alloc();   // 容器复用：每轮读进数据、循环末尾 unref 释放引用
    AVFrame *frame = av_frame_alloc();      // 视频帧:receive 拿到、用完 unref
    AVFrame *audioFrame = av_frame_alloc(); // 音频帧:同理(A2 起用)
    if (!packet || !frame || !audioFrame) {
        std::fprintf(stderr, "分配 packet/frame 失败\n");
        av_frame_free(&audioFrame);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return 1;
    }

    long decodedFrameCount = 0;
    long droppedFrameCount = 0;   // A4:落后音频被丢掉、没显示的帧
    bool firstFramePrinted = false;

    // ===== 阶段⑤ 的状态：YUV → RGB 转换器 + 目标 RGB 缓冲 =====
    // 解码器吐的是 YUV420P,SDL 纹理/显示要 RGB,中间靠 SwsContext 做像素格式转换(见 03)。
    // 懒创建：等拿到第一帧、知道确切宽高和像素格式后再建,比用 codecContext 的值更稳。
    SwsContext *swsContext = nullptr;
    uint8_t *rgbData[4] = {nullptr};  // 目标缓冲的 4 个平面指针(RGB 是 packed,只用 rgbData[0])
    int rgbLinesize[4] = {0};         // 每个平面的行字节数(含对齐填充,可能 > width*3)

    // ===== 阶段⑥ 的状态：SDL 窗口/渲染器/纹理(都懒创建,首帧时按真实尺寸建)=====
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    bool quitRequested = false;       // 用户点了窗口关闭按钮 → 提前结束播放

    // ===== 阶段⑦ 的状态：按 pts 控制节奏 =====
    // time_base 是这一路流的"时间单位"(一个有理数,比如 1/12800 秒)。frame->pts 是以它为单位的整数,
    // pts × time_base = 这帧应该显示的"秒数"。不控制的话会"解多快放多快"——一秒放完整段。
    double timeBaseSeconds = av_q2d(videoStream->time_base);
    Uint32 playbackStartTicks = 0;    // 第一帧显示瞬间的墙钟(ms),作为整段播放的时间原点
    bool playbackStarted = false;

    // 内层:把解码器当前攒着的帧全部取出来。封装成 lambda 复用——drain 阶段(末尾)也要调它。
    auto drainDecodedFrames = [&]() {
        while (true) {
            int receiveResult = avcodec_receive_frame(codecContext, frame);
            // EAGAIN：当前喂的数据还不够吐帧,回去再喂包；EOF：已 flush 完,彻底没帧了。
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                break;
            }
            if (receiveResult < 0) {
                std::fprintf(stderr, "解码出错\n");
                break;
            }
            ++decodedFrameCount;

            // 第一帧打印关键事实,把"解出来到底是什么"看清楚:
            //   - format 是 YUV420P 而非 RGB —— 解码器吐的是 YUV,显示前还要 swscale 转(下一步⑤);
            //   - linesize[0] 往往 > width —— 每行末尾有对齐填充,逐行拷贝必须按 linesize 走(见 02 §linesize);
            //   - pts 是"显示时间戳",单位是 time_base,后面⑦控制节奏要用它换算成秒。
            if (!firstFramePrinted) {
                std::printf("首帧: format=%s, %dx%d, linesize[0]=%d, pts=%lld\n",
                            av_get_pix_fmt_name((AVPixelFormat)frame->format),
                            frame->width, frame->height, frame->linesize[0],
                            (long long)frame->pts);
                firstFramePrinted = true;
            }

            // 懒创建:第一帧时按它的真实宽高/像素格式建好 SwsContext、RGB 缓冲、SDL 窗口与纹理。
            if (!swsContext) {
                swsContext = sws_getContext(
                    frame->width, frame->height, (AVPixelFormat)frame->format,  // 源:YUV420P
                    frame->width, frame->height, AV_PIX_FMT_RGB24,              // 目标:同尺寸 RGB24
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                // 按目标格式分配对齐缓冲;返回的 rgbLinesize[0] 才是真实行宽,后面拷贝都按它走。
                av_image_alloc(rgbData, rgbLinesize,
                               frame->width, frame->height, AV_PIX_FMT_RGB24, 1);

                // SDL 窗口大小 = 视频尺寸。渲染器负责把纹理画上去(优先硬件加速)。
                window = SDL_CreateWindow("simplest_player",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          frame->width, frame->height, SDL_WINDOW_SHOWN);
                renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
                // 纹理像素格式必须和⑤的输出对上:RGB24。STREAMING 表示每帧都会更新它的内容。
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            frame->width, frame->height);
            }

            // --- ⑥ 处理窗口事件:点了关闭按钮就请求退出(否则窗口会"未响应") ---
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    quitRequested = true;
                }
            }
            if (quitRequested) {
                av_frame_unref(frame);
                break;  // 跳出内层取帧循环;外层也会因为这个标志一起停
            }

            // --- A4/⑦ 同步:有音频就以"音频时钟"为主时钟,视频追它;没音频退回墙钟 ---
            // 这帧应在第几秒显示(视频时钟)
            double videoClockSeconds =
                (frame->pts == AV_NOPTS_VALUE) ? 0.0 : frame->pts * timeBaseSeconds;
            if (audioDevice != 0) {
                // 音频时钟 = 声卡"已经播放"的秒数。SDL_GetQueuedAudioSize 返回队列里还没播的字节,
                // 已排入 - 还没播 = 已播放字节,除以每秒字节数 = 已播放秒数。声卡硬件节奏最准,做主时钟。
                uint64_t playedBytes = totalQueuedBytes - SDL_GetQueuedAudioSize(audioDevice);
                double audioClockSeconds = (double)playedBytes / audioBytesPerSecond;
                double driftSeconds = videoClockSeconds - audioClockSeconds;
                if (driftSeconds > 0.0) {
                    SDL_Delay((Uint32)(driftSeconds * 1000.0));  // 视频超前 → 等音频追上
                } else if (driftSeconds < -0.1) {
                    // 视频落后音频 > 100ms → 丢掉这帧追同步(在转换/渲染之前丢,省掉无用功)
                    ++droppedFrameCount;
                    av_frame_unref(frame);
                    continue;
                }
            } else {
                // 没音频:退回墙钟节奏(⑦ 原逻辑),以第一帧为时间原点
                if (!playbackStarted) { playbackStartTicks = SDL_GetTicks(); playbackStarted = true; }
                if (frame->pts != AV_NOPTS_VALUE) {
                    double elapsedMs = SDL_GetTicks() - playbackStartTicks;
                    if (videoClockSeconds * 1000.0 > elapsedMs) {
                        SDL_Delay((Uint32)(videoClockSeconds * 1000.0 - elapsedMs));
                    }
                }
            }

            // --- ⑤ 转换 + ⑥ 渲染:决定要显示这帧了,才 YUV→RGB 并画上去 ---
            // sws_scale 第 4/5 个参数 (0, frame->height) = 从源第 0 行起处理 height 行(整帧)。
            sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height,
                      rgbData, rgbLinesize);
            // UpdateTexture 的行字节数必须传 rgbLinesize[0](不是 width*3)。
            SDL_UpdateTexture(texture, nullptr, rgbData[0], rgbLinesize[0]);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);

            av_frame_unref(frame);  // 取完这帧,释放它持有的缓冲引用,容器留着下轮复用
        }
    };

    // ===== A2：音频版的"取干净"——receive 音频帧 → swr 重采样到 S16 立体声 → 写 WAV =====
    // 结构和视频 drainDecodedFrames 完全对称(send/receive 是通用状态机,音视频都一样)。
    auto drainAudioFrames = [&]() {
        while (true) {
            int receiveResult = avcodec_receive_frame(audioCodecContext, audioFrame);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) break;
            if (receiveResult < 0) { std::fprintf(stderr, "音频解码出错\n"); break; }

            // 输出样本数(每声道)要把重采样器内部缓着的延迟也算上,否则会少几个样本。
            int maxOutSamples = (int)av_rescale_rnd(
                swr_get_delay(swrContext, audioCodecContext->sample_rate) + audioFrame->nb_samples,
                outSampleRate, audioCodecContext->sample_rate, AV_ROUND_UP);

            uint8_t *outBuffer = nullptr;
            av_samples_alloc(&outBuffer, nullptr, outChannels, maxOutSamples, outSampleFormat, 0);

            // swr_convert 返回"实际转出多少样本/声道"。S16 交错下整块数据 = 样本数 × 声道 × 每样本字节。
            int convertedSamples = swr_convert(swrContext, &outBuffer, maxOutSamples,
                                               (const uint8_t **)audioFrame->data,
                                               audioFrame->nb_samples);
            if (convertedSamples > 0 && audioDevice != 0) {
                int byteCount = convertedSamples * outChannels * outBytesPerSample;
                SDL_QueueAudio(audioDevice, outBuffer, byteCount);  // 排进声卡,后台自动播放
                totalQueuedBytes += (uint64_t)byteCount;
            }
            av_freep(&outBuffer);   // 每帧 alloc/free 简单直观;真播放器会复用一块大缓冲
            av_frame_unref(audioFrame);
        }
    };

    // 外层:不停从文件读 packet,视频包喂视频解码器、音频包喂音频解码器。
    while (!quitRequested && av_read_frame(formatContext, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            if (avcodec_send_packet(codecContext, packet) >= 0) {
                drainDecodedFrames();
            }
        } else if (audioCodecContext && packet->stream_index == audioStreamIndex) {
            if (avcodec_send_packet(audioCodecContext, packet) >= 0) {
                drainAudioFrames();
            }
        }
        av_packet_unref(packet);  // 不论哪种包,读进来用完都要 unref(见 01 §5.2)
    }

    // drain(冲刷):文件读完了,但解码器内部可能还攒着几帧(尤其有 B 帧时)。
    // 喂一个 NULL 包告诉它"没有更多输入了",再把残余帧取干净(见 01 §6.4 drain)。
    // 用户中途关窗就不必 drain 了。
    if (!quitRequested) {
        avcodec_send_packet(codecContext, nullptr);
        drainDecodedFrames();
        if (audioCodecContext) {
            avcodec_send_packet(audioCodecContext, nullptr);
            drainAudioFrames();
            // 再冲刷 swr 内部残留的几个样本(传 NULL 输入),排进声卡把尾音播完。
            if (swrContext && audioDevice != 0) {
                uint8_t *tailBuffer = nullptr;
                int tailMax = (int)av_rescale_rnd(
                    swr_get_delay(swrContext, audioCodecContext->sample_rate),
                    outSampleRate, audioCodecContext->sample_rate, AV_ROUND_UP);
                if (tailMax > 0) {
                    av_samples_alloc(&tailBuffer, nullptr, outChannels, tailMax, outSampleFormat, 0);
                    int n = swr_convert(swrContext, &tailBuffer, tailMax, nullptr, 0);
                    if (n > 0) {
                        SDL_QueueAudio(audioDevice, tailBuffer, n * outChannels * outBytesPerSample);
                    }
                    av_freep(&tailBuffer);
                }
            }
        }
    }

    // 文件读完不代表声卡播完——队列里可能还排着没播的音频,等它播干净再退出,否则尾音被切掉。
    if (audioDevice != 0 && !quitRequested) {
        while (SDL_GetQueuedAudioSize(audioDevice) > 0) {
            SDL_Delay(10);
        }
    }

    std::printf("✅ 播放结束,解码 %ld 帧,显示 %ld 帧(丢帧 %ld)\n",
                decodedFrameCount, decodedFrameCount - droppedFrameCount, droppedFrameCount);

    // ===== 清理(初始化建的,退出时 free / close / destroy,见 01 §5.6)=====
    if (audioDevice != 0) SDL_CloseAudioDevice(audioDevice);
    SDL_DestroyTexture(texture);     // 注意:对 nullptr 调用这些 SDL_Destroy 是安全的(空操作)
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    sws_freeContext(swsContext);     // 视频转换器
    swr_free(&swrContext);           // 音频重采样器(对 nullptr 安全)
    av_freep(&rgbData[0]);           // av_image_alloc 分配的 RGB 缓冲(只 free 第 0 个指针)
    av_frame_free(&audioFrame);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&audioCodecContext);  // 对 nullptr 安全(没音频时本就是 nullptr)
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    return 0;
}
