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

    // 以下所有的 AVPacket 和 AVFrame 结构体外壳都是可以并且强烈建议在循环中重复使用的。
    // 它们在循环外只分配一次（av_packet_alloc / av_frame_alloc），
    // 在循环内部每次使用完毕后，通过 av_packet_unref() 或 av_frame_unref() 清空内部数据和状态，
    // 即可在下一次循环中安全复用，避免频繁的内存分配和释放。
    std::unique_ptr<AVPacket, void (*)(AVPacket *)> in_pkt(av_packet_alloc(), [](AVPacket *p) { av_packet_free(&p); });
    std::unique_ptr<AVFrame, void (*)(AVFrame *)> dec_frame(av_frame_alloc(), [](AVFrame *f) { av_frame_free(&f); });
    std::unique_ptr<AVFrame, void (*)(AVFrame *)> sw_frame(av_frame_alloc(), [](AVFrame *f) { av_frame_free(&f); });
    std::unique_ptr<AVFrame, void (*)(AVFrame *)> conv_frame(av_frame_alloc(), [](AVFrame *f) { av_frame_free(&f); });
    std::unique_ptr<AVPacket, void (*)(AVPacket *)> out_pkt(av_packet_alloc(), [](AVPacket *p) { av_packet_free(&p); });
    if (!in_pkt || !dec_frame || !sw_frame || !conv_frame || !out_pkt) return Status::Internal("alloc ffmpeg objects failed");

    if (config.trim_start_us > 0) {
        st = demuxer->Seek(config.trim_start_us);
        if (!st.ok()) return st;
        decoder->Flush();
    }

    int64_t next_pts = 0;
    auto assign_output_pts = [&](AVFrame *src_frame, AVFrame *dst_frame) {
        int64_t src_pts = src_frame->best_effort_timestamp;
        if (src_pts == AV_NOPTS_VALUE) src_pts = src_frame->pts;
        if (src_pts != AV_NOPTS_VALUE) {
            dst_frame->pts = av_rescale_q(src_pts, in_tb, encoder->Context()->time_base);
            if (dst_frame->pts >= next_pts) {
                next_pts = dst_frame->pts + 1;
            } else {
                dst_frame->pts = next_pts++;
            }
            return;
        }
        dst_frame->pts = next_pts++;
    };
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

        // 写音频包
        if (in_pkt->stream_index == demuxer->AudioStreamIndex() && out_audio_stream_index >= 0) {
            st = muxer->WritePacket(in_pkt.get(), demuxer->AudioStreamTimeBase(), out_audio_stream_index);
            if (!st.ok()) return st;
            // 下一个包
            continue;
        }

        // 写视频包
        if (in_pkt->stream_index != demuxer->VideoStreamIndex()) continue;
        
        // 将数据包送给解码器。送包后 in_pkt 即可安全释放（unref），不影响解码器内部状态。
        // FFmpeg 默认且首选“零拷贝”机制：
        // 1. 增加引用计数（Reference，极快/首选）：直接拷贝底层数据指针（AVBufferRef）并将引用计数 +1，实现零拷贝。
        // 2. 拷贝数据（Copy，较慢/备用）：仅在兼容老旧解码器或数据未被引用计数管理时，才会发生实际内存拷贝。
        // （注：即使发生拷贝，压缩数据体积小，耗时极低，不会成为性能瓶颈）
        st = decoder->SendPacket(in_pkt.get());
        if (!st.ok()) return st;

        while (true) {
            av_frame_unref(dec_frame.get());
            bool again = false, dec_eof = false;
            st = decoder->ReceiveFrame(dec_frame.get(), &again, &dec_eof);
            if (!st.ok()) return st;
            if (again || dec_eof) break;

            AVFrame* frame_to_process = dec_frame.get();
            if (dec_frame->format == AV_PIX_FMT_VIDEOTOOLBOX && dec_frame->hw_frames_ctx != nullptr) {
                av_frame_unref(sw_frame.get());
                // 将解码后的图像数据从硬件显存 (dec_frame) 拷贝到系统内存 (sw_frame) 中
                int ret = av_hwframe_transfer_data(sw_frame.get(), dec_frame.get(), 0);
                if (ret < 0) return Status::Ffmpeg("hwframe transfer data failed", ret);
                // 拷贝帧的元数据属性（如 PTS 时间戳、宽高比、颜色空间等），因为上一步只拷贝了像素数据
                av_frame_copy_props(sw_frame.get(), dec_frame.get());
                frame_to_process = sw_frame.get();
            }

            auto process_frame = [&](AVFrame* frame) -> Status {
                av_frame_unref(conv_frame.get());
                Status s = converter->Convert(frame, conv_frame.get());
                if (!s.ok()) return s;

                assign_output_pts(frame, conv_frame.get());

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
                st = filter->SendFrame(frame_to_process);
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
                st = process_frame(frame_to_process);
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

        AVFrame* frame_to_process = dec_frame.get();
        if (dec_frame->format == AV_PIX_FMT_VIDEOTOOLBOX && dec_frame->hw_frames_ctx != nullptr) {
            av_frame_unref(sw_frame.get());
            int ret = av_hwframe_transfer_data(sw_frame.get(), dec_frame.get(), 0);
            if (ret < 0) return Status::Ffmpeg("hwframe transfer data failed", ret);
            av_frame_copy_props(sw_frame.get(), dec_frame.get());
            frame_to_process = sw_frame.get();
        }

        auto process_frame = [&](AVFrame* frame) -> Status {
            av_frame_unref(conv_frame.get());
            Status s = converter->Convert(frame, conv_frame.get());
            if (!s.ok()) return s;
            assign_output_pts(frame, conv_frame.get());
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
            st = filter->SendFrame(frame_to_process);
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
            st = process_frame(frame_to_process);
            if (!st.ok()) return st;
        }
    }

    while (true) {
        st = encoder->SendEof();
        if (st.ok()) break;
        if (st.ffmpeg_error != AVERROR(EAGAIN)) return st;

        bool drained_any = false;
        while (true) {
            av_packet_unref(out_pkt.get());
            bool enc_again = false, enc_eof = false;
            st = encoder->ReceivePacket(out_pkt.get(), &enc_again, &enc_eof);
            if (!st.ok()) return st;
            if (enc_again || enc_eof) break;
            drained_any = true;
            st = muxer->WritePacket(out_pkt.get(), encoder->Context()->time_base, out_video_stream_index);
            if (!st.ok()) return st;
        }
        if (!drained_any) return Status::Ffmpeg("send encoder eof failed: Resource temporarily unavailable", AVERROR(EAGAIN));
    }
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
