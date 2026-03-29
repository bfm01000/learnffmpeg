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
    // 打开输入文件
    ret = avformat_open_input(&fmt_ctx, video_path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to open input file: " << video_path << "\n";
        return ret;
    }
    // 读取流信息
    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to find stream info: " << video_path << "\n";
        return ret;
    }

    int video_stream_index = -1;
    // 找到视频流
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

    // 拿到视频流参数
    AVCodecParameters* codecpar = fmt_ctx->streams[video_stream_index]->codecpar;

    // 拿到解码器
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "Failed to find decoder\n";
        avformat_close_input(&fmt_ctx);
        return AVERROR_DECODER_NOT_FOUND;
    }

    // 初始化解码器上下文
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Failed to allocate codec context\n";
        avformat_close_input(&fmt_ctx);
        return AVERROR(ENOMEM);
    }

    // 将流参数拷贝到解码器上下文
    ret = avcodec_parameters_to_context(codec_ctx, codecpar);
    if (ret < 0) {
        std::cerr << "Failed to copy codec parameters to context\n";
        avformat_close_input(&fmt_ctx);
        return ret;
    }
    // 打开解码器
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Failed to open codec\n";
        avformat_close_input(&fmt_ctx);
        return AVERROR_DECODER_NOT_FOUND;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!pkt || !frame) {
        std::cerr << "Failed to allocate packet or frame\n";
        goto end;
    }

    // 读取视视频帧AVPacket
    while (av_read_frame(fmt_ctx, pkt) >= 0) {

        // 只处理视频流
        if (pkt->stream_index == video_stream_index) {
            
            // 将视频包发送给解码器(压缩数据包->原始数据包)
            ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret < 0) {
                std::cerr << "Failed to send packet\n";
                goto end;
            }
            static int frame_count = 0;
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    // EAGAIN 表示需要更多的 Packet 才能输出 Frame
                    // AVERROR_EOF 表示解码器已经完全输出完毕
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
    const AVCodec* jpeg_codec  = avcodec_find_encoder(AV_CODEC_ID_MJPEG);

    AVCodecContext* codec_ctx = nullptr;
    AVFrame* rgb_frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;

    if (jpeg_codec  == nullptr) {
        std::cerr << "Failed to find encoder\n";
        return false;
    }

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

    codec_ctx = avcodec_alloc_context3(jpeg_codec);
    if (codec_ctx == nullptr) {
        std::cerr << "Failed to allocate jpeg codec context\n";
        return false;
    }

    codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_ctx->codec_id = AV_CODEC_ID_MJPEG;
    codec_ctx->pix_fmt = pix_fmt;
    codec_ctx->width = frame->width;
    codec_ctx->height = frame->height;
    codec_ctx->time_base = AVRational{1, 25};

    ret = avcodec_open2(codec_ctx, jpeg_codec, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to open jpeg encoder\n";
        goto end;
    }

    rgb_frame = av_frame_alloc();
    if (rgb_frame == nullptr) {
        std::cerr << "Failed to allocate jpeg frame\n";
        goto end;
    }

    rgb_frame->format = codec_ctx->pix_fmt;
    rgb_frame->width = codec_ctx->width;
    rgb_frame->height = codec_ctx->height;

    ret = av_frame_get_buffer(rgb_frame, 32);
    if (ret < 0) {
        std::cerr << "Failed to allocate jpeg frame buffer\n";
        goto end;
    }

    ret = av_frame_make_writable(rgb_frame);
    if (ret < 0) {
        std::cerr << "jpeg frame is not writable\n";
        goto end;
    }

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

    sws_scale(
        sws_ctx,
        frame->data,
        frame->linesize,
        0,
        frame->height,
        rgb_frame->data,
        rgb_frame->linesize
    );

    pkt = av_packet_alloc();
    if (pkt == nullptr) {
        std::cerr << "Failed to allocate jpeg packet\n";
        goto end;
    }

    ret = avcodec_send_frame(codec_ctx, rgb_frame);
    if (ret < 0) {
        std::cerr << "Failed to send frame to jpeg encoder\n";
        goto end;
    }

    ret = avcodec_receive_packet(codec_ctx, pkt);
    if (ret < 0) {
        std::cerr << "Failed to receive jpeg packet\n";
        goto end;
    }

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