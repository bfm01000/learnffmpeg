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

    Status st = demuxer->Open(config.input_path.c_str());
    if (!st.ok()) return st;

    st = decoder->Open(
        demuxer->VideoCodecId(),
        demuxer->VideoCodecParameters(),
        config.enable_hw_decode,
        config.preferred_hw_device.c_str());
    if (!st.ok()) return st;

    AVCodecContext *dec_ctx = decoder->Context();

    int out_w = config.target_width > 0 ? config.target_width : demuxer->VideoWidth();
    int out_h = config.target_height > 0 ? config.target_height : demuxer->VideoHeight();
    AVRational frame_rate = demuxer->VideoFrameRate();
    AVRational enc_tb = av_inv_q(frame_rate.num > 0 ? frame_rate : AVRational{config.target_fps, 1});
    AVRational in_tb = demuxer->VideoStreamTimeBase();

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

    int64_t next_pts = 0;
    bool input_eof = false;
    while (!input_eof) {
        av_packet_unref(in_pkt.get());
        st = demuxer->ReadPacket(in_pkt.get(), &input_eof);
        if (!st.ok()) return st;
        if (input_eof) break;

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

            av_frame_unref(conv_frame.get());
            st = converter->Convert(dec_frame.get(), conv_frame.get());
            if (!st.ok()) return st;

            int64_t src_pts = dec_frame->best_effort_timestamp;
            if (src_pts == AV_NOPTS_VALUE) src_pts = dec_frame->pts;
            conv_frame->pts = (src_pts != AV_NOPTS_VALUE)
                                  ? av_rescale_q(src_pts, in_tb, encoder->Context()->time_base)
                                  : next_pts++;

            st = encoder->SendFrame(conv_frame.get());
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

        av_frame_unref(conv_frame.get());
        st = converter->Convert(dec_frame.get(), conv_frame.get());
        if (!st.ok()) return st;
        conv_frame->pts = next_pts++;
        st = encoder->SendFrame(conv_frame.get());
        if (!st.ok()) return st;
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
