#include "media_core/factory/codec_factory.h"

#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/error.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
}

namespace media_core {
namespace {

std::string FfErr(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof(buf));
    return std::string(buf);
}

class FfmpegDemuxer : public IDemuxer {
public:
    ~FfmpegDemuxer() override {
        avformat_close_input(&fmt_ctx_);
    }

    Status Open(const char *input_path) override {
        int ret = avformat_open_input(&fmt_ctx_, input_path, nullptr, nullptr);
        if (ret < 0) return Status::Ffmpeg("avformat_open_input failed: " + FfErr(ret), ret);
        ret = avformat_find_stream_info(fmt_ctx_, nullptr);
        if (ret < 0) return Status::Ffmpeg("avformat_find_stream_info failed: " + FfErr(ret), ret);

        for (unsigned i = 0; i < fmt_ctx_->nb_streams; ++i) {
            const AVMediaType type = fmt_ctx_->streams[i]->codecpar->codec_type;
            if (type == AVMEDIA_TYPE_VIDEO && video_stream_idx_ < 0) {
                video_stream_idx_ = static_cast<int>(i);
                continue;
            }
            if (type == AVMEDIA_TYPE_AUDIO && audio_stream_idx_ < 0) {
                audio_stream_idx_ = static_cast<int>(i);
                continue;
            }
        }
        if (video_stream_idx_ < 0) return Status::NotFound("No video stream found");
        return Status::Ok();
    }

    int VideoStreamIndex() const override { return video_stream_idx_; }

    Status ReadPacket(AVPacket *pkt, bool *eof) override {
        *eof = false;
        int ret = av_read_frame(fmt_ctx_, pkt);
        if (ret == AVERROR_EOF) {
            *eof = true;
            return Status::Ok();
        }
        if (ret < 0) return Status::Ffmpeg("av_read_frame failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

    AVRational VideoStreamTimeBase() const override { return fmt_ctx_->streams[video_stream_idx_]->time_base; }
    int VideoCodecId() const override { return fmt_ctx_->streams[video_stream_idx_]->codecpar->codec_id; }
    const AVCodecParameters *VideoCodecParameters() const override { return fmt_ctx_->streams[video_stream_idx_]->codecpar; }
    int VideoWidth() const override { return fmt_ctx_->streams[video_stream_idx_]->codecpar->width; }
    int VideoHeight() const override { return fmt_ctx_->streams[video_stream_idx_]->codecpar->height; }

    AVRational VideoFrameRate() const override {
        AVRational fr = av_guess_frame_rate(fmt_ctx_, fmt_ctx_->streams[video_stream_idx_], nullptr);
        if (fr.num <= 0 || fr.den <= 0) return AVRational{30, 1};
        return fr;
    }

    bool HasAudioStream() const override { return audio_stream_idx_ >= 0; }
    int AudioStreamIndex() const override { return audio_stream_idx_; }
    AVRational AudioStreamTimeBase() const override { return fmt_ctx_->streams[audio_stream_idx_]->time_base; }
    const AVCodecParameters *AudioCodecParameters() const override { return fmt_ctx_->streams[audio_stream_idx_]->codecpar; }

    Status Seek(int64_t timestamp_us) override {
        // avformat_seek_file is more accurate than av_seek_frame for seeking to a specific timestamp
        int ret = avformat_seek_file(fmt_ctx_, -1, INT64_MIN, timestamp_us, timestamp_us, 0);
        if (ret < 0) return Status::Ffmpeg("avformat_seek_file failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

private:
    AVFormatContext *fmt_ctx_ = nullptr;
    int video_stream_idx_ = -1;
    int audio_stream_idx_ = -1;
};

class FfmpegVideoDecoder : public IVideoDecoder {
public:
    ~FfmpegVideoDecoder() override {
        avcodec_free_context(&ctx_);
    }

    Status Open(int codec_id,
                const AVCodecParameters *codecpar,
                bool /*enable_hw_decode*/,
                const char * /*preferred_hw_device*/) override {
        const AVCodec *decoder = avcodec_find_decoder(static_cast<AVCodecID>(codec_id));
        if (!decoder) return Status::NotFound("Decoder not found");

        ctx_ = avcodec_alloc_context3(decoder);
        if (!ctx_) return Status::Internal("alloc decoder context failed");

        int ret = avcodec_parameters_to_context(ctx_, codecpar);
        if (ret < 0) return Status::Ffmpeg("copy decoder codecpar failed: " + FfErr(ret), ret);

        ret = avcodec_open2(ctx_, decoder, nullptr);
        if (ret < 0) return Status::Ffmpeg("open decoder failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

    AVCodecContext *Context() const override { return ctx_; }

    Status SendPacket(AVPacket *pkt) override {
        int ret = avcodec_send_packet(ctx_, pkt);
        if (ret < 0) return Status::Ffmpeg("send packet failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

    Status SendEof() override {
        int ret = avcodec_send_packet(ctx_, nullptr);
        if (ret < 0) return Status::Ffmpeg("send decoder eof failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

    Status ReceiveFrame(AVFrame *frame, bool *again, bool *eof) override {
        *again = false;
        *eof = false;
        int ret = avcodec_receive_frame(ctx_, frame);
        if (ret == AVERROR(EAGAIN)) {
            *again = true;
            return Status::Ok();
        }
        if (ret == AVERROR_EOF) {
            *eof = true;
            return Status::Ok();
        }
        if (ret < 0) return Status::Ffmpeg("receive frame failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

    Status Flush() override {
        avcodec_flush_buffers(ctx_);
        return Status::Ok();
    }

private:
    AVCodecContext *ctx_ = nullptr;
};

class FfmpegFrameConverter : public IFrameConverter {
public:
    ~FfmpegFrameConverter() override {
        sws_freeContext(sws_);
    }

    Status Init(AVCodecContext *in_ctx, int out_w, int out_h, AVPixelFormat out_fmt) override {
        in_ctx_ = in_ctx;
        out_w_ = out_w;
        out_h_ = out_h;
        out_fmt_ = out_fmt;
        return Status::Ok();
    }

    Status Convert(const AVFrame *in_frame, AVFrame *out_frame) override {
        if (!in_ctx_) return Status::InvalidArg("converter not init");
        out_frame->format = out_fmt_;
        out_frame->width = out_w_;
        out_frame->height = out_h_;

        int ret = av_frame_get_buffer(out_frame, 32);
        if (ret < 0) return Status::Ffmpeg("frame get buffer failed: " + FfErr(ret), ret);
        ret = av_frame_make_writable(out_frame);
        if (ret < 0) return Status::Ffmpeg("frame writable failed: " + FfErr(ret), ret);

        sws_ = sws_getCachedContext(
            sws_,
            in_ctx_->width, in_ctx_->height, in_ctx_->pix_fmt,
            out_w_, out_h_, out_fmt_,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) return Status::Internal("sws_getCachedContext failed");

        ret = sws_scale(sws_, in_frame->data, in_frame->linesize, 0, in_frame->height, out_frame->data, out_frame->linesize);
        if (ret < 0) return Status::Ffmpeg("sws_scale failed", ret);
        return Status::Ok();
    }

private:
    AVCodecContext *in_ctx_ = nullptr;
    SwsContext *sws_ = nullptr;
    int out_w_ = 0;
    int out_h_ = 0;
    AVPixelFormat out_fmt_ = AV_PIX_FMT_YUV420P;
};

class FfmpegVideoEncoder : public IVideoEncoder {
public:
    ~FfmpegVideoEncoder() override {
        avcodec_free_context(&ctx_);
    }

    Status Open(const char *encoder_name,
                int width,
                int height,
                AVRational time_base,
                AVRational frame_rate,
                int bitrate) override {
        const AVCodec *encoder = avcodec_find_encoder_by_name(encoder_name);
        if (!encoder) return Status::NotFound("Encoder not found");
        ctx_ = avcodec_alloc_context3(encoder);
        if (!ctx_) return Status::Internal("alloc encoder context failed");

        ctx_->width = width;
        ctx_->height = height;
        ctx_->time_base = time_base;
        ctx_->framerate = frame_rate;
        ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        ctx_->bit_rate = bitrate;
        ctx_->gop_size = 50;
        ctx_->max_b_frames = 2;

        int ret = avcodec_open2(ctx_, encoder, nullptr);
        if (ret < 0) return Status::Ffmpeg("open encoder failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

    AVCodecContext *Context() const override { return ctx_; }

    Status SendFrame(AVFrame *frame) override {
        int ret = avcodec_send_frame(ctx_, frame);
        if (ret < 0) return Status::Ffmpeg("send frame failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

    Status SendEof() override {
        int ret = avcodec_send_frame(ctx_, nullptr);
        if (ret < 0) return Status::Ffmpeg("send encoder eof failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

    Status ReceivePacket(AVPacket *pkt, bool *again, bool *eof) override {
        *again = false;
        *eof = false;
        int ret = avcodec_receive_packet(ctx_, pkt);
        if (ret == AVERROR(EAGAIN)) {
            *again = true;
            return Status::Ok();
        }
        if (ret == AVERROR_EOF) {
            *eof = true;
            return Status::Ok();
        }
        if (ret < 0) return Status::Ffmpeg("receive packet failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

private:
    AVCodecContext *ctx_ = nullptr;
};

class FfmpegMuxer : public IMuxer {
public:
    ~FfmpegMuxer() override {
        if (fmt_ctx_) {
            if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE) && fmt_ctx_->pb) {
                avio_closep(&fmt_ctx_->pb);
            }
            avformat_free_context(fmt_ctx_);
            fmt_ctx_ = nullptr;
        }
    }

    Status Open(const char *output_path, const char *output_format) override {
        int ret = avformat_alloc_output_context2(&fmt_ctx_, nullptr,
                                                 (output_format && output_format[0]) ? output_format : nullptr,
                                                 output_path);
        if (ret < 0 || !fmt_ctx_) return Status::Ffmpeg("alloc output context failed: " + FfErr(ret), ret);

        if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open(&fmt_ctx_->pb, output_path, AVIO_FLAG_WRITE);
            if (ret < 0) return Status::Ffmpeg("open output file failed: " + FfErr(ret), ret);
        }
        return Status::Ok();
    }

    Status AddVideoStreamFromEncoder(const AVCodecContext *enc_ctx, int *out_stream_index) override {
        AVStream *st = avformat_new_stream(fmt_ctx_, nullptr);
        if (!st) return Status::Internal("new stream failed");
        st->time_base = enc_ctx->time_base;

        int ret = avcodec_parameters_from_context(st->codecpar, enc_ctx);
        if (ret < 0) return Status::Ffmpeg("copy codecpar failed: " + FfErr(ret), ret);
        st->codecpar->codec_tag = 0;
        *out_stream_index = st->index;
        return Status::Ok();
    }

    Status AddAudioStreamFromCodecParameters(const AVCodecParameters *codecpar,
                                             AVRational time_base,
                                             int *out_stream_index) override {
        AVStream *st = avformat_new_stream(fmt_ctx_, nullptr);
        if (!st) return Status::Internal("new audio stream failed");
        st->time_base = time_base;

        int ret = avcodec_parameters_copy(st->codecpar, codecpar);
        if (ret < 0) return Status::Ffmpeg("copy audio codecpar failed: " + FfErr(ret), ret);
        st->codecpar->codec_tag = 0;
        *out_stream_index = st->index;
        return Status::Ok();
    }

    Status WriteHeader() override {
        int ret = avformat_write_header(fmt_ctx_, nullptr);
        if (ret < 0) return Status::Ffmpeg("write header failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

    Status WritePacket(AVPacket *pkt, AVRational src_tb, int out_stream_index) override {
        AVStream *st = fmt_ctx_->streams[out_stream_index];
        av_packet_rescale_ts(pkt, src_tb, st->time_base);
        pkt->stream_index = out_stream_index;
        int ret = av_interleaved_write_frame(fmt_ctx_, pkt);
        if (ret < 0) return Status::Ffmpeg("write frame failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

    Status WriteTrailer() override {
        int ret = av_write_trailer(fmt_ctx_);
        if (ret < 0) return Status::Ffmpeg("write trailer failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

private:
    AVFormatContext *fmt_ctx_ = nullptr;
};

class FfmpegCapabilityProbe : public ICapabilityProbe {
public:
    std::vector<std::string> QueryDecoderHwDevices(int codec_id) const override {
        std::vector<std::string> out;
        const AVCodec *decoder = avcodec_find_decoder(static_cast<AVCodecID>(codec_id));
        if (!decoder) return out;

        for (int i = 0;; ++i) {
            const AVCodecHWConfig *cfg = avcodec_get_hw_config(decoder, i);
            if (!cfg) break;
            if (!(cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) continue;
            const char *name = av_hwdevice_get_type_name(cfg->device_type);
            if (name) out.push_back(name);
        }
        return out;
    }
};

class FfmpegVideoFilter : public IVideoFilter {
public:
    ~FfmpegVideoFilter() override {
        if (graph_) avfilter_graph_free(&graph_);
    }

    Status Init(const char* filter_desc, int in_width, int in_height, int in_pix_fmt, AVRational in_tb, AVRational in_sar) override {
        graph_ = avfilter_graph_alloc();
        if (!graph_) return Status::Internal("avfilter_graph_alloc failed");

        char args[512];
        snprintf(args, sizeof(args),
                 "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                 in_width, in_height, in_pix_fmt,
                 in_tb.num, in_tb.den,
                 in_sar.num, in_sar.den);

        const AVFilter* buffersrc = avfilter_get_by_name("buffer");
        const AVFilter* buffersink = avfilter_get_by_name("buffersink");

        int ret = avfilter_graph_create_filter(&src_ctx_, buffersrc, "in", args, nullptr, graph_);
        if (ret < 0) return Status::Ffmpeg("create buffer filter failed", ret);

        ret = avfilter_graph_create_filter(&sink_ctx_, buffersink, "out", nullptr, nullptr, graph_);
        if (ret < 0) return Status::Ffmpeg("create buffersink filter failed", ret);

        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();

        outputs->name = av_strdup("in");
        outputs->filter_ctx = src_ctx_;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = sink_ctx_;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        ret = avfilter_graph_parse_ptr(graph_, filter_desc, &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        if (ret < 0) return Status::Ffmpeg("avfilter_graph_parse_ptr failed: " + FfErr(ret), ret);

        ret = avfilter_graph_config(graph_, nullptr);
        if (ret < 0) return Status::Ffmpeg("avfilter_graph_config failed: " + FfErr(ret), ret);

        return Status::Ok();
    }

    Status SendFrame(AVFrame* frame) override {
        int ret = av_buffersrc_add_frame_flags(src_ctx_, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
        if (ret < 0) return Status::Ffmpeg("av_buffersrc_add_frame failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

    Status ReceiveFrame(AVFrame* frame, bool* again, bool* eof) override {
        *again = false;
        *eof = false;
        int ret = av_buffersink_get_frame(sink_ctx_, frame);
        if (ret == AVERROR(EAGAIN)) {
            *again = true;
            return Status::Ok();
        }
        if (ret == AVERROR_EOF) {
            *eof = true;
            return Status::Ok();
        }
        if (ret < 0) return Status::Ffmpeg("av_buffersink_get_frame failed: " + FfErr(ret), ret);
        return Status::Ok();
    }

private:
    AVFilterGraph* graph_ = nullptr;
    AVFilterContext* src_ctx_ = nullptr;
    AVFilterContext* sink_ctx_ = nullptr;
};

}  // namespace

std::unique_ptr<IDemuxer> CodecFactory::CreateDemuxer() { return std::unique_ptr<IDemuxer>(new FfmpegDemuxer()); }
std::unique_ptr<IVideoDecoder> CodecFactory::CreateVideoDecoder() { return std::unique_ptr<IVideoDecoder>(new FfmpegVideoDecoder()); }
std::unique_ptr<IFrameConverter> CodecFactory::CreateFrameConverter() { return std::unique_ptr<IFrameConverter>(new FfmpegFrameConverter()); }
std::unique_ptr<IVideoEncoder> CodecFactory::CreateVideoEncoder() { return std::unique_ptr<IVideoEncoder>(new FfmpegVideoEncoder()); }
std::unique_ptr<IMuxer> CodecFactory::CreateMuxer() { return std::unique_ptr<IMuxer>(new FfmpegMuxer()); }
std::unique_ptr<ICapabilityProbe> CodecFactory::CreateCapabilityProbe() { return std::unique_ptr<ICapabilityProbe>(new FfmpegCapabilityProbe()); }
std::unique_ptr<IVideoFilter> CodecFactory::CreateVideoFilter() { return std::unique_ptr<IVideoFilter>(new FfmpegVideoFilter()); }

}  // namespace media_core
