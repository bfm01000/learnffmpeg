#include <iostream>
#include <string>
#include <fstream>

extern "C"{
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/frame.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}



void parse_args(int argc, char** argv, std::string& video_path, int& n, std::string& output_path) {

    if (argc != 7) {
        std::cerr << "Usage: " << argv[0]
        << " --input <video_path> --n <n> --output <output_path>\n";
        std::exit(101);
    }
  
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            video_path = argv[++i]; // 获取下一个参数并跳过
        } else if (arg == "--n" && i + 1 < argc) {
            n = std::stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        }   
    }
    if (video_path.empty()) {
        std::cerr << "Missing input file\n";
        std::exit(101);
    }
    if (n <= 0) {
        std::cerr << "n must be greater than 0\n";
        std::exit(101);
    }
    if (output_path.empty()) {
        std::cerr << "Missing output file\n";
        std::exit(101);
    }
}

bool save_frame_as_jpeg(AVFrame* frame, const std::string& output_path);
bool save_frame_as_ppm(AVFrame* frame, const std::string& output_path);

bool has_suffix(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool save_frame_by_extension(AVFrame* frame, const std::string& output_path) {
    if (has_suffix(output_path, ".jpg") || has_suffix(output_path, ".jpeg")) {
        return save_frame_as_jpeg(frame, output_path);
    }
    if (has_suffix(output_path, ".ppm")) {
        return save_frame_as_ppm(frame, output_path);
    }

    std::cerr << "Unsupported output format: " << output_path << "\n";
    std::cerr << "Please use .jpg/.jpeg or .ppm\n";
    return false;
}

/**
 * 1. 打开输入文件
 * 2. 找到视频流
 * 3. 拿到视频流参数
 * 4. 初始化解码器
 * 5. 读取视频帧AVPacket（可能为IDR，I，P，B），之后解码成AVFrame
 * 6. 保存AVFrame到文件
 * 7. 关闭输入文件
 */

int main(int argc, char** argv) {

    std::string output_path;
    std::string video_path;
    int n = 0;
    int ret = 0;
    parse_args(argc, argv, video_path, n, output_path);

    AVFormatContext* fmt_ctx = nullptr;
    // 1. 打开输入文件并解析容器头
    // 注：avformat_open_input 会读取文件头（如 MP4 的 moov box），识别出封装格式，
    // 并自动分配 AVFormatContext 结构体。这个结构体是整个媒体文件的“大管家”，
    // 包含了文件的全局信息以及所有的流（视频、音频、字幕等）的初步信息。
    ret = avformat_open_input(&fmt_ctx, video_path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to open input file: " << video_path << "\n";
        return ret;
    }
    // 2. 探测并读取流的详细信息
    // 注：有些文件格式（如 MPEG-TS）的头部信息不全。avformat_find_stream_info 会尝试
    // 读取文件中的一部分数据包进行解码探测，从而获取每个流的详细参数（如帧率、精确的编解码器等），
    // 补全 fmt_ctx->streams 里的信息。
    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to find stream info: " << video_path << "\n";
        return ret;
    }

    int video_stream_index = -1;
    // 3. 在所有流中找到视频流的索引
    // 注：一个媒体文件通常包含多个流（Stream），比如流0是视频，流1是音频。
    // 我们遍历所有的流，通过 codec_type 检查它是不是视频流（AVMEDIA_TYPE_VIDEO），
    // 找到后记录下它的索引，后续我们只处理这个索引对应的数据包。
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            std::cout << "Video stream found at index: " << i << "\n";
            video_stream_index = i;
            break;
        }
    }
    if (video_stream_index == -1) {
        std::cout << "No video stream found\n";
        avformat_close_input(&fmt_ctx);
        return AVERROR_STREAM_NOT_FOUND;
    }

    // 4. 拿到视频流的编码参数
    // 注：codecpar 包含了该视频流的静态参数，比如宽、高、像素格式，以及最重要的 codec_id（如 H.264/HEVC）。
    AVCodecParameters* codecpar = fmt_ctx->streams[video_stream_index]->codecpar;

    // 5. 根据 codec_id 查找对应的解码器（算法实现）
    // 注：avcodec_find_decoder 会在 FFmpeg 注册的解码器库中，寻找能处理该 codec_id 的解码器。
    // 找到的 codec 是一个包含函数指针的静态单例对象，代表了具体的解码算法。
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "Failed to find decoder\n";
        avformat_close_input(&fmt_ctx);
        return AVERROR_DECODER_NOT_FOUND;
    }

    // 初始化解码器上下文
    // 注：这里不仅分配了内存，还会将 codec 的指针保存到 codec_ctx->codec 中。
    // 这样 codec_ctx 就知道自己该用哪套“算法逻辑”了。
    // 此时的 codec_ctx 只是一个“空壳”，里面的参数（宽高、格式等）都是默认值。
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Failed to allocate codec context\n";
        avformat_close_input(&fmt_ctx);
        return AVERROR(ENOMEM);
    }

    // 将流参数拷贝到解码器上下文
    // 注：解码器上下文（codec_ctx）目前还是空的默认值。我们需要把从文件中读到的
    // 视频流参数（如宽、高、SPS/PPS 额外数据等）复制给它，这样解码器在启动时
    // 才知道要处理的具体视频规格。
    ret = avcodec_parameters_to_context(codec_ctx, codecpar);
    if (ret < 0) {
        std::cerr << "Failed to copy codec parameters to context\n";
        avformat_close_input(&fmt_ctx);
        return ret;
    }
    // 打开解码器
    // 注：这一步正式将 codec_ctx（参数状态）和 codec（核心算法）绑定并启动。
    // 为什么这里还要再传一次 codec？
    // 因为在某些特殊场景下（比如用户想强制指定一个特定的解码器实现，或者 codec_ctx 是通过其他方式创建且没有绑定 codec 的），
    // 允许在这里覆盖或显式指定 codec。通常情况下，如果传 nullptr，FFmpeg 会自动使用 codec_ctx->codec。
    // 这一步会根据 codec_ctx 中的参数（如宽高），为解码器分配内部私有内存（priv_data）并初始化底层算法。
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Failed to open codec\n";
        avformat_close_input(&fmt_ctx);
        return AVERROR_DECODER_NOT_FOUND;
    }

    // 分配 AVPacket 和 AVFrame
    // 注：
    // AVPacket：用来存放解封装后、解码前的压缩数据（例如一帧 H.264 的二进制数据）。
    // AVFrame：用来存放解码后的原始未压缩数据（例如一帧 YUV 或 RGB 像素数据）。
    // 这里只是分配了外层的结构体“壳子”，里面的 data 指针目前还是空的，后续读取/解码时才会真正分配或引用内存。
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!pkt || !frame) {
        std::cerr << "Failed to allocate packet or frame\n";
        goto end;
    }

    // 循环读取数据包
    // 注：av_read_frame 会从容器中读取下一个 AVPacket。
    // 注意它读取的是交织的数据包（可能一会儿是视频，一会儿是音频），所以需要判断 stream_index。
    while (av_read_frame(fmt_ctx, pkt) >= 0) {

        // 过滤：只处理我们关心的视频流数据包
        if (pkt->stream_index == video_stream_index) {
            
            // 将视频包发送给解码器(压缩数据包->原始数据包)
            // 注：虽然我们传的是 codec_ctx，但 FFmpeg 内部会通过 codec_ctx->codec 
            // 找到对应的 AVCodec，然后调用它里面的 decode() 函数指针来执行真正的解码逻辑。
            // 相当于面向对象里的 `this->vtable->decode(this, pkt)`。
            ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret < 0) {
                std::cerr << "Failed to send packet\n";
                goto end;
            }
            static int frame_count = 0;
            // 尝试从解码器接收解码后的原始画面 (AVFrame)
            // 注：解码是一个异步且非 1:1 的过程。比如遇到 B 帧时，解码器可能需要吃进去好几个 AVPacket，
            // 才能吐出一个 AVFrame。因此需要用 while 循环不断尝试接收，直到解码器说“目前没有帧了”。
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    // EAGAIN: 解码器内部的数据不够，需要你继续 send 新的 AVPacket 才能输出。
                    // AVERROR_EOF: 已经发送过 flush 包（通常在文件末尾），解码器里的所有帧都吐干净了。
                    break;
                } else if (ret < 0) {
                    std::cerr << "Error receiving frame from decoder\n";
                    goto end; // 发生严重错误，跳出
                }

                // 成功获取到一帧解码后的数据 (AVFrame)
                std::cout << "Decoded frame: width=" << frame->width 
                << " height=" << frame->height 
                << " format=" << frame->format 
                << " frame_count=" << frame_count++
                << "\n";

                if (!save_frame_by_extension(frame, output_path + "/RGB_" + std::to_string(frame_count) + ".ppm")) {
                    ret = -1;
                }
                if (frame_count >= n) {
      
                    goto end;
                }
            }

        }
        // 释放 AVPacket 内部引用的数据缓冲区
        // 注：av_read_frame 会为 pkt->data 分配内存或增加引用计数。
        // 用完之后必须 unref，否则会导致严重的内存泄漏。
        av_packet_unref(pkt);
    }


    end:
    // 5. 释放所有分配的资源
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
                                                                                                                                 
    // 读取视频帧
    return 0;
}


bool save_frame_as_jpeg(AVFrame* frame, const std::string& output_path) {
    bool success = false;
    int ret = 0;

    // 1. 查找 JPEG 编码器
    // 注：和解码类似，这里通过 ID 找到负责将原始像素压缩成 JPEG 格式的算法实现。
    const AVCodec* jpeg_codec  = avcodec_find_encoder(AV_CODEC_ID_MJPEG);

    AVCodecContext* codec_ctx = nullptr;
    AVFrame* rgb_frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;

    if (jpeg_codec  == nullptr) {
        std::cerr << "Failed to find encoder\n";
        return false;
    }

    // 2. 确定编码器支持的像素格式
    // 注：不同的编码器对输入像素格式有特定要求。比如 JPEG 编码器通常更喜欢 YUVJ420P。
    // 这里通过 avcodec_get_supported_config 询问该编码器支持哪些像素格式，
    // 并默认取它支持的第一个格式（通常是最佳格式）。如果没有获取到，则回退到 YUV420P。
    AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;
    const void* supported_configs = nullptr;
    int num_configs = 0;

    ret = avcodec_get_supported_config(
        nullptr,
        jpeg_codec,
        AV_CODEC_CONFIG_PIX_FORMAT,
        0,
        &supported_configs,
        &num_configs
    );
    if (ret >= 0 && supported_configs != nullptr && num_configs > 0) {
        pix_fmt = static_cast<const AVPixelFormat*>(supported_configs)[0];
    }

    // 3. 初始化并配置编码器上下文
    // 注：分配编码器的工作环境，并手动填入编码参数。
    // 解码时参数是从文件中读出来的，而编码时参数是我们主动设置给编码器的。
    codec_ctx = avcodec_alloc_context3(jpeg_codec);
    if (codec_ctx == nullptr) {
        std::cerr << "Failed to allocate jpeg codec context\n";
        return false;
    }

    codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_ctx->codec_id = AV_CODEC_ID_MJPEG;
    codec_ctx->pix_fmt = pix_fmt;                // 告诉编码器我们将喂给它什么格式的像素
    codec_ctx->width = frame->width;             // 目标图片的宽
    codec_ctx->height = frame->height;           // 目标图片的高
    codec_ctx->time_base = AVRational{1, 25};    // 时间基（对于单张图片来说随便设一个即可）

    // 4. 打开编码器，绑定算法与参数
    ret = avcodec_open2(codec_ctx, jpeg_codec, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to open jpeg encoder\n";
        goto end;
    }

    // 5. 准备用于存放转换后像素的输入帧 (rgb_frame 这个变量名在这里其实是用于存放转换后的 YUV 数据)
    // 注：因为原视频帧(frame)的像素格式可能不是 JPEG 编码器想要的(pix_fmt)，
    // 所以我们需要分配一个新的 AVFrame，用来存放格式转换后的像素数据。
    rgb_frame = av_frame_alloc();
    if (rgb_frame == nullptr) {
        std::cerr << "Failed to allocate jpeg frame\n";
        goto end;
    }

    rgb_frame->format = codec_ctx->pix_fmt;
    rgb_frame->width = codec_ctx->width;
    rgb_frame->height = codec_ctx->height;

    // 为这个新帧分配实际的内存缓冲区
    ret = av_frame_get_buffer(rgb_frame, 32);
    if (ret < 0) {
        std::cerr << "Failed to allocate jpeg frame buffer\n";
        goto end;
    }

    // 确保缓冲区可写
    ret = av_frame_make_writable(rgb_frame);
    if (ret < 0) {
        std::cerr << "jpeg frame is not writable\n";
        goto end;
    }

    // 6. 执行像素格式转换
    // 注：创建 swscale 转换上下文，将源帧 (frame) 的像素格式转换成编码器需要的格式 (pix_fmt)。
    sws_ctx = sws_getContext(
        frame->width,
        frame->height,
        static_cast<AVPixelFormat>(frame->format),
        frame->width,
        frame->height,
        codec_ctx->pix_fmt,
        SWS_BICUBIC,
        nullptr,
        nullptr,
        nullptr
    );
    if (sws_ctx == nullptr) {
        std::cerr << "Failed to create sws context\n";
        goto end;
    }

    // 将转换后的像素数据写入 rgb_frame 的缓冲区中
    sws_scale(
        sws_ctx,
        frame->data,
        frame->linesize,
        0,
        frame->height,
        rgb_frame->data,
        rgb_frame->linesize
    );

    // 7. 将像素数据发送给编码器进行压缩
    // 注：分配一个 AVPacket 用来接收压缩后的 JPEG 二进制数据。
    pkt = av_packet_alloc();
    if (pkt == nullptr) {
        std::cerr << "Failed to allocate jpeg packet\n";
        goto end;
    }

    // avcodec_send_frame：把装满原始像素的 AVFrame 喂给编码器。
    // 这和解码时的 send_packet 刚好是反向操作。
    ret = avcodec_send_frame(codec_ctx, rgb_frame);
    if (ret < 0) {
        std::cerr << "Failed to send frame to jpeg encoder\n";
        goto end;
    }

    // avcodec_receive_packet：从编码器接收压缩好的 JPEG 数据包。
    // 因为是单张图片编码，通常发送一帧就会立刻输出一个 packet。
    ret = avcodec_receive_packet(codec_ctx, pkt);
    if (ret < 0) {
        std::cerr << "Failed to receive jpeg packet\n";
        goto end;
    }

    // 8. 将压缩后的 JPEG 数据写入文件
    {
        std::ofstream ofs(output_path, std::ios::binary);
        if (!ofs) {
            std::cerr << "Failed to open output file: " << output_path << "\n";
            goto end;
        }
        ofs.write(reinterpret_cast<const char*>(pkt->data), pkt->size);
    }

    success = true;

end:
    sws_freeContext(sws_ctx);
    av_packet_free(&pkt);
    av_frame_free(&rgb_frame);
    avcodec_free_context(&codec_ctx);
    return success;
}

bool save_frame_as_ppm(AVFrame* frame, const std::string& output_path) {
    // 用返回值表示整套“像素格式转换 + 写文件”流程是否成功。
    bool success = false;

    // rgb_frame 用来存放转换后的 RGB24 图像数据。
    // sws_ctx 是 swscale 提供的像素格式转换上下文。
    // ret 统一保存 FFmpeg API 的返回值，便于判断是否出错。
    AVFrame* rgb_frame = av_frame_alloc();
    SwsContext* sws_ctx = nullptr;
    int ret = 0;

    // 先分配一个目标帧对象；它只是“壳子”，后面还要给它分配真正的图像缓冲区。
    if (rgb_frame == nullptr) {
        std::cerr << "Failed to allocate ppm frame\n";
        return false;
    }

    // PPM(P6) 需要写入 RGB 原始像素，因此目标帧的像素格式必须是 RGB24。
    // 同时目标图片的宽高直接沿用源视频帧的宽高。
    rgb_frame->format = AV_PIX_FMT_RGB24;
    rgb_frame->width = frame->width;
    rgb_frame->height = frame->height;

    // 给目标帧分配真正的图像缓冲区。
    // 这里的 32 是对齐要求，FFmpeg 会按这个对齐值分配每一行的数据。
    ret = av_frame_get_buffer(rgb_frame, 32);
    if (ret < 0) {
        std::cerr << "Failed to allocate ppm frame buffer\n";
        goto end;
    }

    // 确保目标帧的缓冲区可写。
    // 这里可以把三个步骤连起来理解：
    // 1. av_frame_alloc() 只分配 AVFrame 这个“壳子”，此时 data/linesize 还没有真正指向图像像素内存。
    // 2. av_frame_get_buffer() 才会按照 format/width/height 为这个壳子分配实际的图像缓冲区。
    // 3. av_frame_make_writable() 则是在“准备写入像素数据”前做最后一次确认：
    //    如果当前缓冲区已经是当前 frame 独占且可写，就直接返回成功；
    //    如果它引用的是共享缓冲区，FFmpeg 会在必要时为当前 frame 重新准备一份可写缓冲区。
    // 之所以要做这一步，是因为后面的 sws_scale() 会把转换后的 RGB 像素真正写到 rgb_frame->data 中。
    ret = av_frame_make_writable(rgb_frame);
    if (ret < 0) {
        std::cerr << "ppm frame is not writable\n";
        goto end;
    }

    // 创建像素格式转换上下文：
    // - 输入：源帧的宽、高、像素格式
    // - 输出：同样的宽、高，但像素格式转换成 RGB24
    // PPM 不接受 YUV420P/NV12 这类视频像素格式，所以必须先转成 RGB。
    sws_ctx = sws_getContext(
        frame->width,
        frame->height,
        static_cast<AVPixelFormat>(frame->format),
        frame->width,
        frame->height,
        AV_PIX_FMT_RGB24,
        SWS_BICUBIC,
        nullptr,
        nullptr,
        nullptr
    );
    if (sws_ctx == nullptr) {
        std::cerr << "Failed to create sws context for ppm\n";
        goto end;
    }

    // 执行真正的像素格式转换，把源 frame 中的像素数据写入 rgb_frame。
    // sws_scale(...) 这几个参数可以这样理解：
    // 1. sws_ctx
    //    前面 sws_getContext() 创建出来的转换上下文，里面保存了
    //    “从源宽高/像素格式 -> 目标宽高/像素格式”的转换规则。
    // 2. frame->data
    //    源图像每个 plane 的起始地址。
    //    例如 YUV420P 会有 Y/U/V 三个 plane；RGB24 通常主要用 data[0]。
    // 3. frame->linesize
    //    源图像每个 plane 一行实际占用的字节数，也叫 stride。
    //    它不一定等于 width * bytes_per_pixel，因为很多图像缓冲区会有对齐填充。
    // 4. 0
    //    表示从源图像的第 0 行开始转换；也就是从顶部开始处理整张图。
    // 5. frame->height
    //    表示这次要转换多少行。这里传整个高度，意味着整帧全部转换。
    // 6. rgb_frame->data
    //    目标图像每个 plane 的起始地址，转换后的像素会被写到这里。
    // 7. rgb_frame->linesize
    //    目标图像每个 plane 一行对应的字节跨度，sws_scale 会按这个跨度写入。
    sws_scale(
        sws_ctx,
        frame->data,
        frame->linesize,
        0,
        frame->height,
        rgb_frame->data,
        rgb_frame->linesize
    );

    // 以二进制方式打开输出文件，准备写入 PPM。
    {
        std::ofstream ofs(output_path, std::ios::binary);
        if (!ofs) {
            std::cerr << "Failed to open output file: " << output_path << "\n";
            goto end;
        }

        // 写入 PPM(P6) 文件头：
        // P6           -> 表示这是二进制 PPM
        // width height -> 图片宽高
        // 255          -> 每个颜色通道的最大值
        ofs << "P6\n" << rgb_frame->width << " " << rgb_frame->height << "\n255\n";

        // 逐行写入 RGB 像素数据。
        // 每个像素 3 字节(R/G/B)，所以一行写 width * 3 字节。
        // 不能直接一次性写整个缓冲区，因为每行实际跨度要按 linesize 走。
        for (int y = 0; y < rgb_frame->height; ++y) {
            ofs.write(
                reinterpret_cast<const char*>(rgb_frame->data[0] + y * rgb_frame->linesize[0]),
                rgb_frame->width * 3
            );
        }
    }

    // 能走到这里说明图片已经成功保存。
    success = true;

end:
    // 无论成功失败，都释放转换上下文和目标帧，避免内存泄漏。
    sws_freeContext(sws_ctx);
    av_frame_free(&rgb_frame);
    return success;
}