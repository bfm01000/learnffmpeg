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

    // ===== 后续阶段(占位,逐步填) =====
    // ② 找视频流 / 读宽高
    // ③ 建解码器
    // ④ 解码循环
    // ⑤ YUV→RGB
    // ⑥ SDL 显示
    // ⑦ 节奏控制 + 清理

    // 打开用 open / 关闭用 close：avformat_close_input 会释放上下文并把指针置空(见 01 §5.4)
    avformat_close_input(&formatContext);
    return 0;
}
