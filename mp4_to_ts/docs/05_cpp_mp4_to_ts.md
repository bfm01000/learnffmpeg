# Day 6：C++ 调用 FFmpeg API 实现 MP4 转 TS

## 1. 本节目标

- 不靠命令行，直接在 C++ 代码里完成 MP4 → TS。
- 掌握最小 remux 流程：读包、映射流、重映射时间戳、写包。
- 了解何时需要 `h264_mp4toannexb` bitstream filter。

---

## 2. API 级转换和命令行转换的关系

- 命令行 `ffmpeg -i in.mp4 ... out.ts` 本质也是调用同一套库（`libavformat/libavcodec`）。
- C++ API 的优势是：可嵌入业务流程、精细控制日志/错误处理/流选择策略。
- 这节先做“封装转换（remux）”，即通常不重编码；下一步再考虑编码器链路。

---

## 3. 最小可用流程（remux）

1. `avformat_open_input` 打开输入 MP4。
2. `avformat_find_stream_info` 读取流信息。
3. `avformat_alloc_output_context2(..., "mpegts", ...)` 创建输出 TS 上下文。
4. 为每个输入流创建输出流：`avformat_new_stream` + `avcodec_parameters_copy`。
5. 打开输出 IO：`avio_open`，写文件头：`avformat_write_header`。
6. 循环 `av_read_frame` 读包，做时间戳转换 `av_packet_rescale_ts`，调用 `av_interleaved_write_frame` 写包。
7. 结束时 `av_write_trailer`，再释放资源。

---

## 4. C++ 示例代码（remux 版）

> 这是教学版最小代码，便于理解流程。可先跑通，再做错误处理增强。

```cpp
#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

static std::string fferr2str(int errnum) {
    // FFmpeg 错误码是 int（通常为负值），这里把它转换成可读字符串，便于日志排查。
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_make_error_string(buf, sizeof(buf), errnum);
    return std::string(buf);
}

int remux_mp4_to_ts(const std::string& in_path, const std::string& out_path) {
    // 输入容器上下文：代表 input.mp4
    AVFormatContext* ifmt_ctx = nullptr;
    // 输出容器上下文：代表 output.ts
    AVFormatContext* ofmt_ctx = nullptr;
    // 输入流索引 -> 输出流索引的映射表。
    // 例如 input 的视频流是 0、音频流是 2，则可能映射成 output 的 0、1。
    // 对于被过滤掉的流，保持 -1。
    std::vector<int> stream_map;
    int ret = 0;

    // Step 1: 打开输入文件，并让 ifmt_ctx 指向已探测出的输入容器。
    if ((ret = avformat_open_input(&ifmt_ctx, in_path.c_str(), nullptr, nullptr)) < 0) {
        std::cerr << "open input failed: " << fferr2str(ret) << "\n";
        return ret;
    }
    // Step 2: 读取并补全输入流信息（码流参数、时基、时长等）。
    // 很多字段在 open_input 后并不完整，需要这一步。
    if ((ret = avformat_find_stream_info(ifmt_ctx, nullptr)) < 0) {
        std::cerr << "find stream info failed: " << fferr2str(ret) << "\n";
        avformat_close_input(&ifmt_ctx);
        return ret;
    }

    // Step 3: 创建输出容器上下文。
    // 这里显式指定 "mpegts"，表示输出封装格式为 TS。
    // out_path 仅用于辅助推断与记录。
    if ((ret = avformat_alloc_output_context2(&ofmt_ctx, nullptr, "mpegts", out_path.c_str())) < 0 || !ofmt_ctx) {
        std::cerr << "alloc output ctx failed: " << fferr2str(ret) << "\n";
        avformat_close_input(&ifmt_ctx);
        return ret < 0 ? ret : AVERROR_UNKNOWN;
    }

    // Step 4: 预分配映射表，初始值全为 -1（表示默认不转该流）。
    stream_map.resize(ifmt_ctx->nb_streams, -1);
    // 输出流的连续索引计数器。
    int out_index = 0;
    // 遍历输入容器里的所有流，决定哪些流要拷贝到输出。
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; ++i) {
        AVStream* in_stream = ifmt_ctx->streams[i];
        AVCodecParameters* in_par = in_stream->codecpar;

        // 只保留音频/视频/字幕流。其他流（如附件、数据流）先跳过。
        if (in_par->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_par->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_par->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            continue;
        }

        // Step 5: 在输出容器中创建对应输出流（不绑定编码器，remux 场景不用重编码）。
        AVStream* out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        if (!out_stream) {
            ret = AVERROR(ENOMEM);
            std::cerr << "new stream failed\n";
            goto end;
        }

        // 记录输入流 i 对应到输出流 out_index。
        stream_map[i] = out_index++;
        // 把输入流的编码参数完整复制到输出流。
        // remux 的核心：不改编码参数，只换容器。
        // 这些“准备好的参数”会放在 out_stream->codecpar 中，
        // 后面的 avformat_write_header 会读取它们来生成容器头信息。
        ret = avcodec_parameters_copy(out_stream->codecpar, in_par);
        if (ret < 0) {
            std::cerr << "copy codecpar failed: " << fferr2str(ret) << "\n";
            goto end;
        }
        // 这里把 codec_tag 清零，表示“不要强行沿用输入容器里的 tag 值”。
        // 原因：codec_tag 是“容器私有的编码标识”（fourcc/标记），不同封装规则不同。
        // 例如 MP4 里某个 tag 在 TS 容器中可能不合法或不推荐，直接拷贝可能导致写头失败、
        // 播放器兼容性问题，或 muxer 给出 tag 不匹配警告。
        // 清零后，FFmpeg 的 TS muxer 会根据 codec_id 自动选择该容器最合适的 tag。
        // 对 remux 来说，这通常是更安全、更通用的做法。
        out_stream->codecpar->codec_tag = 0;
    }

    // Step 6: 打开输出目标（文件/网络）。
    // ofmt_ctx->oformat->flags 是“当前输出封装格式的能力/行为位标志集合”（bit flags）。
    // 这里用按位与判断 AVFMT_NOFILE 是否被置位：
    //   (flags & AVFMT_NOFILE) != 0  => 该 muxer 不需要你手动打开文件 IO（例如某些特殊/内存输出）
    //   (flags & AVFMT_NOFILE) == 0  => 该 muxer 需要外部 AVIOContext，所以要 avio_open
    // 代码里写成 !(flags & AVFMT_NOFILE)，意思就是“只有在需要文件 IO 时才打开输出”。
    // 小位运算示意（假设 AVFMT_NOFILE = 0b0100）：
    //   case A: flags = 0b1101 -> flags & 0b0100 = 0b0100(非0) -> !true  -> false，不调用 avio_open
    //   case B: flags = 0b1001 -> flags & 0b0100 = 0b0000(为0) -> !false -> true， 调用 avio_open
    // 常见 flags（输出封装能力）补充：
    //   AVFMT_NOFILE       : muxer 不需要外部 AVIO（不必 avio_open）。
    //   AVFMT_GLOBALHEADER : 该容器倾向使用全局头（编码器常需配合 AV_CODEC_FLAG_GLOBAL_HEADER）。
    //   AVFMT_VARIABLE_FPS : 容器支持可变帧率。
    //   AVFMT_TS_NONSTRICT : 时间戳规则可放宽（非严格场景更宽容）。
    //   AVFMT_NOSTREAMS    : 允许没有流也能进行某些写入流程（少见）。
    //   AVFMT_ALLOW_FLUSH  : 支持 flush 行为（例如写入 NULL 包触发刷新）。
    // 注意：flags 是“封装格式能力描述”，不是“当前文件实时状态”。
    // 对 mpegts 文件输出，通常会进入这个分支并打开 out_path。
    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::cerr << "open output failed: " << fferr2str(ret) << "\n";
            goto end;
        }
    }
    ·
    // Step 7: 写容器头。到这里输出文件才真正进入可写数据包状态。
    // 关键理解：
    //   avcodec_parameters_copy 只是准备元数据（写到各个 out_stream->codecpar），不是写媒体包。
    //   avformat_write_header 会遍历 ofmt_ctx->streams，读取每个流的 codecpar/time_base 等信息，
    //   并据此让 muxer 初始化输出头部。头写完后，才能进入后面的写包循环。
    //   所以流程是“先准备流参数 -> 写头 -> 再写真实音视频数据包”。
    ret = avformat_write_header(ofmt_ctx, nullptr);
    if (ret < 0) {
        std::cerr << "write header failed: " << fferr2str(ret) << "\n";
        goto end;
    }

    // Step 8: 主循环——读一个输入包，处理后写一个输出包。
    while (true) {
        AVPacket pkt;
        // 初始化 AVPacket（老 API 风格，教学示例里可用）。
        av_init_packet(&pkt);
        // 从输入容器读取下一个包（可能是视频包，也可能是音频包）。
        ret = av_read_frame(ifmt_ctx, &pkt);
        // ret < 0 通常表示 EOF 或错误，这里统一退出循环。
        if (ret < 0) {
            break;
        }

        AVStream* in_stream = ifmt_ctx->streams[pkt.stream_index];
        // 若该输入流不在映射表中（值为 -1），直接丢弃此包。
        if (pkt.stream_index < 0 || pkt.stream_index >= (int)stream_map.size() || stream_map[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }
        // 改写为输出流索引，后续 muxer 才知道把它写到哪个输出流。
        pkt.stream_index = stream_map[pkt.stream_index];
        AVStream* out_stream = ofmt_ctx->streams[pkt.stream_index];

        // Step 9: 时间戳重映射（非常关键）。
        // 输入输出流的 time_base 常常不同，必须换算 pkt 的 pts/dts/duration。
        // 否则极易出现“时间戳非单调”“音画不同步”等问题。
        av_packet_rescale_ts(&pkt, in_stream->time_base, out_stream->time_base);
        // 对文件输出而言，原始输入包位置无意义，置为 -1。
        pkt.pos = -1;

        // Step 10: 交织写包。
        // 对多流（音视频）来说，interleaved 版本会更稳地维护输出顺序。
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        // 无论写成功失败，都要释放当前包引用，避免内存泄漏。
        av_packet_unref(&pkt);
        if (ret < 0) {
            std::cerr << "write frame failed: " << fferr2str(ret) << "\n";
            goto end;
        }
    }

    // Step 11: 写容器尾（trailer），刷新缓冲与索引等收尾信息。
    av_write_trailer(ofmt_ctx);
    // 能执行到这里，认为流程成功。
    ret = 0;

end:
    // Step 12: 统一资源释放（无论成功/失败都走这里）。
    // 先关输入。
    avformat_close_input(&ifmt_ctx);
    // 再关输出 IO（如果打开过）。
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE) && ofmt_ctx->pb) {
        avio_closep(&ofmt_ctx->pb);
    }
    // 最后释放输出上下文。
    if (ofmt_ctx) {
        avformat_free_context(ofmt_ctx);
    }
    // 返回 0 表示成功，负值表示失败。
    return ret;
}

int main(int argc, char* argv[]) {
    // 命令行参数：程序名 + 输入 + 输出，共 3 个。
    if (argc != 3) {
        std::cerr << "usage: remux <input.mp4> <output.ts>\n";
        return 1;
    }
    // 设置 FFmpeg 日志级别，便于调试时观察内部信息。
    av_log_set_level(AV_LOG_INFO);
    // 执行核心 remux。
    int ret = remux_mp4_to_ts(argv[1], argv[2]);
    if (ret < 0) {
        std::cerr << "remux failed: " << ret << "\n";
        return 1;
    }
    std::cout << "done\n";
    return 0;
}
```

---

## 5. 编译与运行示例

```bash
g++ -std=c++17 remux.cpp -o remux \
  $(pkg-config --cflags --libs libavformat libavcodec libavutil)

./remux input.mp4 output.ts
ffprobe -hide_banner -show_streams output.ts
```

---

## 6. 关键点讲解

1. **为什么要 `avcodec_parameters_copy`**  
   remux 不改编码参数，直接把输入流参数复制到输出流。

2. **为什么要 `av_packet_rescale_ts`**  
   输入流和输出流的 `time_base` 可能不同。直接写会导致 DTS/PTS 混乱，必须重映射。

3. **为什么用 `av_interleaved_write_frame`**  
   让 FFmpeg 处理多流交织顺序，更稳妥。

4. **常见报错：DTS 非单调**  
   通常与时间戳转换/输入源异常有关，先检查 `av_packet_rescale_ts` 与输入流时间戳。

---

## 7. 进阶：H.264 的 Annex B 问题

- 有些 MP4 中 H.264 为 AVCC 格式，写入 TS 时常需要 Annex B。
- 命令行里对应 `-bsf:v h264_mp4toannexb`。
- API 里可用 `libavcodec/bsf.h` 创建 `h264_mp4toannexb` 过滤器，在写包前对视频包做过滤。

如果你的最小 remux 在某些播放器异常（黑屏/花屏），优先补这一步。

---

## 8. 一页总结

- C++ 做 MP4→TS，本质是：**打开输入 → 建立输出流映射 → 读包 → 重映射时间戳 → 写包**。
- 先跑通 remux，再补充 bitstream filter 与更完整错误处理。
- 产物一定用 `ffprobe` 验证，确认流类型、编码、时长都合理。
