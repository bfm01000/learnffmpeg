#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

static std::string fferr2str(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_make_error_string(buf, sizeof(buf), errnum);
    return std::string(buf);
}

int remux_mp4_to_ts(const std::string& in_path, const std::string& out_path) {
    AVFormatContext* ifmt_ctx = nullptr;
    AVFormatContext* ofmt_ctx = nullptr;
    std::vector<int> stream_map;
    int ret = 0;

    if ((ret = avformat_open_input(&ifmt_ctx, in_path.c_str(), nullptr, nullptr)) < 0) {
        std::cerr << "open input failed: " << fferr2str(ret) << "\n";
        return ret;
    }
    if ((ret = avformat_find_stream_info(ifmt_ctx, nullptr)) < 0) {
        std::cerr << "find stream info failed: " << fferr2str(ret) << "\n";
        avformat_close_input(&ifmt_ctx);
        return ret;
    }

    if ((ret = avformat_alloc_output_context2(&ofmt_ctx, nullptr, "mpegts", out_path.c_str())) < 0 || !ofmt_ctx) {
        std::cerr << "alloc output ctx failed: " << fferr2str(ret) << "\n";
        avformat_close_input(&ifmt_ctx);
        return ret < 0 ? ret : AVERROR_UNKNOWN;
    }

    stream_map.resize(ifmt_ctx->nb_streams, -1);
    int out_index = 0;
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; ++i) {
        AVStream* in_stream = ifmt_ctx->streams[i];
        AVCodecParameters* in_par = in_stream->codecpar;

        if (in_par->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_par->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_par->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            continue;
        }

        AVStream* out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        if (!out_stream) {
            ret = AVERROR(ENOMEM);
            std::cerr << "new stream failed\n";
            goto end;
        }

        stream_map[i] = out_index++;
        ret = avcodec_parameters_copy(out_stream->codecpar, in_par);
        if (ret < 0) {
            std::cerr << "copy codecpar failed: " << fferr2str(ret) << "\n";
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
    }

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::cerr << "open output failed: " << fferr2str(ret) << "\n";
            goto end;
        }
    }

    ret = avformat_write_header(ofmt_ctx, nullptr);
    if (ret < 0) {
        std::cerr << "write header failed: " << fferr2str(ret) << "\n";
        goto end;
    }

    while (true) {
        AVPacket pkt;
        av_init_packet(&pkt);
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0) {
            break;
        }

        AVStream* in_stream = ifmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index < 0 || pkt.stream_index >= (int)stream_map.size() || stream_map[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }
        pkt.stream_index = stream_map[pkt.stream_index];
        AVStream* out_stream = ofmt_ctx->streams[pkt.stream_index];

        av_packet_rescale_ts(&pkt, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        av_packet_unref(&pkt);
        if (ret < 0) {
            std::cerr << "write frame failed: " << fferr2str(ret) << "\n";
            goto end;
        }
    }

    av_write_trailer(ofmt_ctx);
    ret = 0;

end:
    avformat_close_input(&ifmt_ctx);
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE) && ofmt_ctx->pb) {
        avio_closep(&ofmt_ctx->pb);
    }
    if (ofmt_ctx) {
        avformat_free_context(ofmt_ctx);
    }
    return ret;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "usage: remux <input.mp4> <output.ts>\n";
        return 1;
    }
    av_log_set_level(AV_LOG_INFO);
    int ret = remux_mp4_to_ts(argv[1], argv[2]);
    if (ret < 0) {
        std::cerr << "remux failed: " << ret << "\n";
        return 1;
    }
    std::cout << "done\n";
    return 0;
}