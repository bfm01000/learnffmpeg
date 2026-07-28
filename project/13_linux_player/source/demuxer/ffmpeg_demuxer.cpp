/// @file ffmpeg_demuxer.cpp
/// @brief FFmpegDemuxer — 基于 FFmpeg 的容器解封装.

#include "source/demuxer/ffmpeg_demuxer.h"

#include <cerrno>

extern "C" {
#include <libavformat/avformat.h>
}

namespace player {

// ── 辅助: AVMediaType → MediaType ──────────────────────────────────────

MediaType fromAVMediaType(int avType) {
  switch (avType) {
    case AVMEDIA_TYPE_VIDEO:    return MediaType::Video;
    case AVMEDIA_TYPE_AUDIO:    return MediaType::Audio;
    case AVMEDIA_TYPE_SUBTITLE: return MediaType::Subtitle;
    default:                    return MediaType::Unknown;
  }
}

// ── 生命周期 ────────────────────────────────────────────────────────────

FFmpegDemuxer::FFmpegDemuxer() = default;

FFmpegDemuxer::~FFmpegDemuxer() {
  close();
}

// ── open ─────────────────────────────────────────────────────────────────

int FFmpegDemuxer::open(const char* url) {
  if (!url || !*url) return -EINVAL;

  // 确保先关闭之前可能打开的资源
  close();

  // v1: 直接使用 avformat_open_input, FFmpeg 内部处理协议.
  //     后续通过自定义 AVIO 对接 ProtocolFactory.
  int ret = avformat_open_input(&m_fmtCtx, url, nullptr, nullptr);
  if (ret < 0) {
    m_fmtCtx = nullptr;
    return ret;
  }

  // 读取流信息（可能触发网络 IO 读取一部分数据进行分析）
  ret = avformat_find_stream_info(m_fmtCtx, nullptr);
  if (ret < 0) {
    avformat_close_input(&m_fmtCtx);
    return ret;
  }

  // 填充流元信息
  buildStreamInfos_();

  return 0;
}

// ── readPacket ──────────────────────────────────────────────────────────

std::shared_ptr<AVPacket> FFmpegDemuxer::readPacket() {
  if (!m_fmtCtx) return nullptr;

  AVPacket* pkt = av_packet_alloc();
  if (!pkt) return nullptr;

  int ret = av_read_frame(m_fmtCtx, pkt);
  if (ret < 0) {
    av_packet_free(&pkt);
    return nullptr;   // EOF 或错误, 上层通过返回值 + errno 判断
  }

  // shared_ptr + 自定义 deleter: 确保 AVPacket 正确释放
  return std::shared_ptr<AVPacket>(pkt, [](AVPacket* p) {
    av_packet_free(&p);
  });
}

// ── seekTo ──────────────────────────────────────────────────────────────

int FFmpegDemuxer::seekTo(int64_t posMs) {
  if (!m_fmtCtx) return -EBADF;

  int64_t timestamp = av_rescale(posMs, AV_TIME_BASE, 1000);

  // AVSEEK_FLAG_BACKWARD: seek 到目标位置之前最近的关键帧
  int ret = av_seek_frame(m_fmtCtx, -1, timestamp, AVSEEK_FLAG_BACKWARD);
  return ret;
}

// ── getStreams ──────────────────────────────────────────────────────────

std::vector<StreamInfo> FFmpegDemuxer::getStreams() const {
  return m_streamInfos;
}

// ── close ───────────────────────────────────────────────────────────────

int FFmpegDemuxer::close() {
  if (m_fmtCtx) {
    avformat_close_input(&m_fmtCtx);
    m_fmtCtx = nullptr;
  }
  m_streamInfos.clear();
  return 0;
}

// ── 内部 ────────────────────────────────────────────────────────────────

void FFmpegDemuxer::buildStreamInfos_() {
  m_streamInfos.clear();
  if (!m_fmtCtx) return;

  for (unsigned i = 0; i < m_fmtCtx->nb_streams; ++i) {
    AVStream* st = m_fmtCtx->streams[i];
    AVCodecParameters* par = st->codecpar;

    StreamInfo info;
    info.index    = static_cast<int>(i);
    info.type     = fromAVMediaType(par->codec_type);
    info.codecId  = par->codec_id;
    info.codecName= avcodec_get_name(par->codec_id) ?: "unknown";
    info.bitrate  = par->bit_rate;
    info.duration = st->duration > 0
                    ? av_rescale_q(st->duration, st->time_base, AV_TIME_BASE_Q)
                    : -1;

    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
      info.width  = par->width;
      info.height = par->height;
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
      info.sampleRate = par->sample_rate;
      info.channels   = par->ch_layout.nb_channels;
    }

    m_streamInfos.push_back(info);
  }
}

} // namespace player
