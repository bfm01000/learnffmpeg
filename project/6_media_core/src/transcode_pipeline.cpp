#include "media_core/pipeline/transcode_pipeline.h"

#include <memory>

#include "media_core/factory/codec_factory.h"

extern "C" {
#include <libavcodec/packet.h>
}

namespace media_core {

Status TranscodePipeline::Run(const VideoTranscodeConfig &config) {
    if (config.input_path.empty() || config.output_path.empty()) {
        return Status::InvalidArg("input_path/output_path is empty");
    }

    std::unique_ptr<IDemuxer> demuxer = CodecFactory::CreateDemuxer();
    std::unique_ptr<IVideoDecoder> decoder = CodecFactory::CreateVideoDecoder();
    std::unique_ptr<IFrameConverter> converter = CodecFactory::CreateFrameConverter();
    std::unique_ptr<IVideoEncoder> encoder = CodecFactory::CreateVideoEncoder();
    std::unique_ptr<IMuxer> muxer = CodecFactory::CreateMuxer();

    // 打开复用器
    Status st = demuxer->Open(config.input_path.c_str());
    if (!st.ok()) return st;

    // 打开解码器
    st = decoder->Open(
        demuxer->VideoCodecId(),
        demuxer->VideoCodecParameters(),
        config.enable_hw_decode,
        config.preferred_hw_device.c_str());
    if (!st.ok()) return st;

    // 获取解码器上下文
    AVCodecContext *dec_ctx = decoder->Context();

    // 获取输入时间基
    AVRational in_tb = demuxer->VideoStreamTimeBase();
    
    // 初始化视频过滤器
    std::unique_ptr<IVideoFilter> filter = CodecFactory::CreateVideoFilter();
    bool use_filter = !config.filter_desc.empty();
    if (use_filter) {
        AVRational sar = dec_ctx->sample_aspect_ratio;
        if (sar.num == 0) sar = {1, 1};
        st = filter->Init(config.filter_desc.c_str(), dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt, in_tb, sar);
        if (!st.ok()) return st;
    }

    int out_w = config.target_width > 0 ? config.target_width : demuxer->VideoWidth();
    int out_h = config.target_height > 0 ? config.target_height : demuxer->VideoHeight();
    AVRational frame_rate = demuxer->VideoFrameRate();
    AVRational enc_tb = av_inv_q(frame_rate.num > 0 ? frame_rate : AVRational{config.target_fps, 1});

    st = converter->Init(dec_ctx, out_w, out_h, AV_PIX_FMT_YUV420P);
    if (!st.ok()) return st;

    const char *enc_name = config.video_encoder_name.empty() ? "libx264" : config.video_encoder_name.c_str();
    st = encoder->Open(enc_name, out_w, out_h, enc_tb, frame_rate, config.video_bitrate);
    if (!st.ok()) return st;

    st = muxer->Open(config.output_path.c_str(), config.output_format.c_str());
    if (!st.ok()) return st;

    int out_video_stream_index = -1;
    st = muxer->AddVideoStreamFromEncoder(encoder->Context(), &out_video_stream_index);
    if (!st.ok()) return st;

    int out_audio_stream_index = -1;
    if (demuxer->HasAudioStream()) {
        st = muxer->AddAudioStreamFromCodecParameters(
            demuxer->AudioCodecParameters(),
            demuxer->AudioStreamTimeBase(),
            &out_audio_stream_index);
        if (!st.ok()) return st;
    }
    st = muxer->WriteHeader();
    if (!st.ok()) return st;

    std::unique_ptr<AVPacket, void (*)(AVPacket *)> in_pkt(av_packet_alloc(), [](AVPacket *p) { av_packet_free(&p); });
    std::unique_ptr<AVFrame, void (*)(AVFrame *)> dec_frame(av_frame_alloc(), [](AVFrame *f) { av_frame_free(&f); });
    std::unique_ptr<AVFrame, void (*)(AVFrame *)> conv_frame(av_frame_alloc(), [](AVFrame *f) { av_frame_free(&f); });
    std::unique_ptr<AVPacket, void (*)(AVPacket *)> out_pkt(av_packet_alloc(), [](AVPacket *p) { av_packet_free(&p); });
    if (!in_pkt || !dec_frame || !conv_frame || !out_pkt) return Status::Internal("alloc ffmpeg objects failed");

    if (config.trim_start_us > 0) {
        st = demuxer->Seek(config.trim_start_us);
        if (!st.ok()) return st;
        decoder->Flush();
    }

    int64_t next_pts = 0;
    bool input_eof = false;
    while (!input_eof) {
        av_packet_unref(in_pkt.get());
        st = demuxer->ReadPacket(in_pkt.get(), &input_eof);
        if (!st.ok()) return st;
        if (input_eof) break;

        // Check trim end
        if (config.trim_end_us > 0) {
            int64_t pkt_pts_us = -1;
            if (in_pkt->pts != AV_NOPTS_VALUE) {
                AVRational tb = (in_pkt->stream_index == demuxer->VideoStreamIndex()) ? demuxer->VideoStreamTimeBase() : demuxer->AudioStreamTimeBase();
                pkt_pts_us = av_rescale_q(in_pkt->pts, tb, {1, AV_TIME_BASE});
            }
            if (pkt_pts_us >= config.trim_end_us) {
                break; // Reached the end of the trimmed segment
            }
        }

        if (in_pkt->stream_index == demuxer->AudioStreamIndex() && out_audio_stream_index >= 0) {
            st = muxer->WritePacket(in_pkt.get(), demuxer->AudioStreamTimeBase(), out_audio_stream_index);
            if (!st.ok()) return st;
            continue;
        }
        if (in_pkt->stream_index != demuxer->VideoStreamIndex()) continue;
        st = decoder->SendPacket(in_pkt.get());
        if (!st.ok()) return st;

        while (true) {
            av_frame_unref(dec_frame.get());
            bool again = false, dec_eof = false;
            st = decoder->ReceiveFrame(dec_frame.get(), &again, &dec_eof);
            if (!st.ok()) return st;
            if (again || dec_eof) break;

            auto process_frame = [&](AVFrame* frame) -> Status {
                av_frame_unref(conv_frame.get());
                Status s = converter->Convert(frame, conv_frame.get());
                if (!s.ok()) return s;

                int64_t src_pts = frame->best_effort_timestamp;
                if (src_pts == AV_NOPTS_VALUE) src_pts = frame->pts;
                conv_frame->pts = (src_pts != AV_NOPTS_VALUE)
                                      ? av_rescale_q(src_pts, in_tb, encoder->Context()->time_base)
                                      : next_pts++;

                s = encoder->SendFrame(conv_frame.get());
                if (!s.ok()) return s;

                while (true) {
                    av_packet_unref(out_pkt.get());
                    bool enc_again = false, enc_eof = false;
                    s = encoder->ReceivePacket(out_pkt.get(), &enc_again, &enc_eof);
                    if (!s.ok()) return s;
                    if (enc_again || enc_eof) break;
                    s = muxer->WritePacket(out_pkt.get(), encoder->Context()->time_base, out_video_stream_index);
                    if (!s.ok()) return s;
                }
                return Status::Ok();
            };

            if (use_filter) {
                st = filter->SendFrame(dec_frame.get());
                if (!st.ok()) return st;
                while (true) {
                    std::unique_ptr<AVFrame, void(*)(AVFrame*)> filt_frame(av_frame_alloc(), [](AVFrame* f){ av_frame_free(&f); });
                    bool filt_again = false, filt_eof = false;
                    st = filter->ReceiveFrame(filt_frame.get(), &filt_again, &filt_eof);
                    if (!st.ok()) return st;
                    if (filt_again || filt_eof) break;

                    st = process_frame(filt_frame.get());
                    if (!st.ok()) return st;
                }
            } else {
                st = process_frame(dec_frame.get());
                if (!st.ok()) return st;
            }
        }
    }

    st = decoder->SendEof();
    if (!st.ok()) return st;
    while (true) {
        av_frame_unref(dec_frame.get());
        bool again = false, dec_eof = false;
        st = decoder->ReceiveFrame(dec_frame.get(), &again, &dec_eof);
        if (!st.ok()) return st;
        if (again || dec_eof) break;

        auto process_frame = [&](AVFrame* frame) -> Status {
            av_frame_unref(conv_frame.get());
            Status s = converter->Convert(frame, conv_frame.get());
            if (!s.ok()) return s;
            conv_frame->pts = next_pts++;
            s = encoder->SendFrame(conv_frame.get());
            if (!s.ok()) return s;
            return Status::Ok();
        };

        if (use_filter) {
            st = filter->SendFrame(dec_frame.get());
            if (!st.ok()) return st;
            while (true) {
                std::unique_ptr<AVFrame, void(*)(AVFrame*)> filt_frame(av_frame_alloc(), [](AVFrame* f){ av_frame_free(&f); });
                bool filt_again = false, filt_eof = false;
                st = filter->ReceiveFrame(filt_frame.get(), &filt_again, &filt_eof);
                if (!st.ok()) return st;
                if (filt_again || filt_eof) break;

                st = process_frame(filt_frame.get());
                if (!st.ok()) return st;
            }
        } else {
            st = process_frame(dec_frame.get());
            if (!st.ok()) return st;
        }
    }

    st = encoder->SendEof();
    if (!st.ok()) return st;
    while (true) {
        av_packet_unref(out_pkt.get());
        bool enc_again = false, enc_eof = false;
        st = encoder->ReceivePacket(out_pkt.get(), &enc_again, &enc_eof);
        if (!st.ok()) return st;
        if (enc_again || enc_eof) break;
        st = muxer->WritePacket(out_pkt.get(), encoder->Context()->time_base, out_video_stream_index);
        if (!st.ok()) return st;
    }

    return muxer->WriteTrailer();
}

}  // namespace media_core
