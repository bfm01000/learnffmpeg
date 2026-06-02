// 最简视频播放器：demux MP4 → decode H.264 → swscale 转 RGB → SDL 显示
// 对应 Doc/ffmpeg/99-学习进度.md 优先级 1。只播视频、不含音频。
//
// 整体框架(7 步,逐步填充):
//   ① 解封装：打开文件、找视频流              ← 本步已完成
//   ② 读视频流信息(宽高/编码)
//   ③ 建解码器(AVCodec + AVCodecContext)
//   ④ 解码循环(send_packet / receive_frame)
//   ⑤ YUV → RGB(SwsContext)
//   ⑥ SDL 开窗、把 RGB 画上去
//   ⑦ 按 pts 控制播放节奏 + drain + 清理
//
// 用法: ./simplest_player <视频文件.mp4>

extern "C" {
// FFmpeg 是 C 库,在 C++ 里必须用 extern "C" 包,否则链接时找不到符号(见 01 §8.1)
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <cstdio>

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

    // ===== 后续阶段(占位,逐步填) =====
    // ③ 建解码器
    // ④ 解码循环
    // ⑤ YUV→RGB
    // ⑥ SDL 显示
    // ⑦ 节奏控制 + 清理

    // 打开用 open / 关闭用 close：avformat_close_input 会释放上下文并把指针置空(见 01 §5.4)
    avformat_close_input(&formatContext);
    return 0;
}
