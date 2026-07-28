#include "ffmpeg_demuxer.h"

namespace player {

FFmpegDemuxer::FFmpegDemuxer() {}

FFmpegDemuxer::FFmpegDemuxer(std::unique_ptr<IProtocolHandler> protocol_handler)
    : protocol_handler_(std::move(protocol_handler)) {}

FFmpegDemuxer::~FFmpegDemuxer() {
    close();
}

int FFmpegDemuxer::open(const char* url) {
    // TODO: implement full open logic
    // 1. If protocol_handler_ is set, use it for custom I/O
    // 2. Otherwise, let avformat_open_input handle protocol detection
    // 3. Call avformat_open_input(&fmt_ctx_, url, nullptr, nullptr)
    // 4. Call avformat_find_stream_info(fmt_ctx_, nullptr)
    // 5. Populate stream_infos_ via buildStreamInfos()
    return -1;
}

std::shared_ptr<AVPacket> FFmpegDemuxer::readPacket() {
    // TODO: implement packet reading
    // 1. Allocate AVPacket via av_packet_alloc()
    // 2. Call av_read_frame(fmt_ctx_, pkt)
    // 3. On success, return shared_ptr<AVPacket>(pkt, [](AVPacket* p){ av_packet_free(&p); })
    // 4. On EOF (AVERROR_EOF) or error, free pkt and return nullptr
    return nullptr;
}

int FFmpegDemuxer::seekTo(int64_t pos_ms) {
    // TODO: implement seeking
    // 1. Convert pos_ms to AV_TIME_BASE
    // 2. Call av_seek_frame(fmt_ctx_, -1, timestamp, AVSEEK_FLAG_BACKWARD)
    // 3. Return 0 on success, negative on error
    return -1;
}

std::vector<StreamInfo> FFmpegDemuxer::getStreams() const {
    return stream_infos_;
}

int FFmpegDemuxer::close() {
    // TODO: implement cleanup
    // 1. avformat_close_input(&fmt_ctx_) if fmt_ctx_ is not null
    // 2. Reset state
    if (fmt_ctx_) {
        avformat_close_input(&fmt_ctx_);
        fmt_ctx_ = nullptr;
    }
    stream_infos_.clear();
    opened_ = false;
    return 0;
}

AVFormatContext* FFmpegDemuxer::formatContext() const {
    return fmt_ctx_;
}

void FFmpegDemuxer::buildStreamInfos() {
    // TODO: iterate fmt_ctx_->streams and build StreamInfo entries
    // Map AVMediaType to MediaType, extract codec parameters
}

} // namespace player
