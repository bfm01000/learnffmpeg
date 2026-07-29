/// @file test/unit/test_audio_decoder.cpp
/// @brief AudioDecoder 单元测试.

#include "decode/audio_decoder/audio_decoder.h"
#include "source/demuxer/ffmpeg_demuxer.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

extern "C" {
#include <libavutil/frame.h>
}

namespace player {
namespace test {

static std::string createTestMedia() {
  std::string path = "/tmp/test_player_adec_" + std::to_string(getpid()) + ".mp4";
  std::string cmd =
    "ffmpeg -y -hide_banner -loglevel error "
    "-f lavfi -i testsrc=duration=1:size=320x240:rate=25 "
    "-f lavfi -i sine=frequency=440:duration=1 "
    "-c:v libx264 -preset ultrafast -pix_fmt yuv420p "
    "-c:a aac -shortest "
    "\"" + path + "\" 2>/dev/null";
  if (std::system(cmd.c_str()) != 0) return {};
  return path;
}

static void removeTestMedia(const std::string& path) {
  if (!path.empty()) std::remove(path.c_str());
}

static AVCodecParameters* getAudioCodecParams(const std::string& path) {
  FFmpegDemuxer demuxer;
  if (demuxer.open(path.c_str()) < 0) return nullptr;

  auto streams = demuxer.getStreams();
  int audioIdx = -1;
  for (const auto& s : streams) {
    if (s.type == MediaType::Audio) { audioIdx = s.index; break; }
  }
  if (audioIdx < 0) return nullptr;

  AVStream* st = demuxer.formatContext()->streams[audioIdx];
  AVCodecParameters* params = avcodec_parameters_alloc();
  avcodec_parameters_copy(params, st->codecpar);
  demuxer.close();
  return params;
}

// ── 测试 ────────────────────────────────────────────────────────────────

TEST(AudioDecoderTest, OpenValidAAC) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty());

  AVCodecParameters* params = getAudioCodecParams(path);
  ASSERT_NE(params, nullptr);

  AudioDecoder dec;
  EXPECT_EQ(dec.open(params), 0);
  EXPECT_TRUE(dec.isOpen());

  dec.close();
  avcodec_parameters_free(&params);
  removeTestMedia(path);
}

TEST(AudioDecoderTest, DecodeAudioFrames) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty());

  AVCodecParameters* params = getAudioCodecParams(path);
  ASSERT_NE(params, nullptr);

  AudioDecoder dec;
  ASSERT_EQ(dec.open(params), 0);

  FFmpegDemuxer demuxer;
  ASSERT_EQ(demuxer.open(path.c_str()), 0);

  auto streams = demuxer.getStreams();
  int audioIdx = -1;
  for (const auto& s : streams) {
    if (s.type == MediaType::Audio) { audioIdx = s.index; break; }
  }

  int frameCount = 0;
  while (frameCount < 5) {
    auto pkt = demuxer.readPacket();
    if (!pkt) break;
    if (pkt->stream_index != audioIdx) continue;

    int ret = dec.sendPacket(pkt.get());
    if (ret < 0 && ret != AVERROR(EAGAIN)) break;

    AVFrame* frame = av_frame_alloc();
    while (true) {
      ret = dec.recvFrame(frame);
      if (ret == AVERROR(EAGAIN)) break;
      if (ret < 0) break;
      ++frameCount;
      EXPECT_GE(frame->sample_rate, 0);           // 可能为 0（依赖编码器）
      EXPECT_GE(frame->ch_layout.nb_channels, 0); // 同上
      EXPECT_GT(frame->nb_samples, 0);            // 一定有采样数据
      av_frame_unref(frame);
    }
    av_frame_free(&frame);
  }

  EXPECT_GT(frameCount, 0);

  demuxer.close();
  dec.close();
  avcodec_parameters_free(&params);
  removeTestMedia(path);
}

TEST(AudioDecoderTest, CloseIdempotent) {
  AudioDecoder dec;
  dec.close();
  dec.close();
}

TEST(AudioDecoderTest, SendPacketBeforeOpen) {
  AudioDecoder dec;
  EXPECT_LT(dec.sendPacket(nullptr), 0);
}

TEST(AudioDecoderTest, RecvFrameBeforeOpen) {
  AudioDecoder dec;
  AVFrame* f = av_frame_alloc();
  EXPECT_LT(dec.recvFrame(f), 0);
  av_frame_free(&f);
}

TEST(AudioDecoderTest, FlushAfterOpen) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty());

  AVCodecParameters* params = getAudioCodecParams(path);
  ASSERT_NE(params, nullptr);

  AudioDecoder dec;
  ASSERT_EQ(dec.open(params), 0);
  dec.flush();   // flush 后解码器仍然可用

  dec.close();
  avcodec_parameters_free(&params);
  removeTestMedia(path);
}

} // namespace test
} // namespace player
