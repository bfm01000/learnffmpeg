/// @file test/unit/test_video_decoder.cpp
/// @brief VideoDecoder 单元测试.

#include "decode/video_decoder/video_decoder.h"
#include "source/demuxer/ffmpeg_demuxer.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

extern "C" {
#include <libavutil/frame.h>
}

namespace player {
namespace test {

// ── 测试辅助 ─────────────────────────────────────────────────────────────

/// 用 ffmpeg CLI 生成测试 MP4 文件
static std::string createTestMedia() {
  std::string path = "/tmp/test_player_vdec_" + std::to_string(getpid()) + ".mp4";
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

/// 从测试文件中提取视频流的 AVCodecParameters。
/// 返回 avcodec_parameters_alloc() 分配的副本，调用者负责 avcodec_parameters_free()。
static AVCodecParameters* getVideoCodecParams(const std::string& path) {
  FFmpegDemuxer demuxer;
  if (demuxer.open(path.c_str()) < 0) return nullptr;

  auto streams = demuxer.getStreams();
  int videoIdx = -1;
  for (const auto& s : streams) {
    if (s.type == MediaType::Video) {
      videoIdx = s.index;
      break;
    }
  }
  if (videoIdx < 0) return nullptr;

  AVStream* st = demuxer.formatContext()->streams[videoIdx];
  AVCodecParameters* params = avcodec_parameters_alloc();
  avcodec_parameters_copy(params, st->codecpar);
  demuxer.close();
  return params;
}

// ── 测试 ────────────────────────────────────────────────────────────────

TEST(VideoDecoderTest, OpenValidH264) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty());

  AVCodecParameters* params = getVideoCodecParams(path);
  ASSERT_NE(params, nullptr);

  VideoDecoder dec;
  EXPECT_EQ(dec.open(params), 0);
  EXPECT_TRUE(dec.isOpen());

  dec.close();
  avcodec_parameters_free(&params);
  removeTestMedia(path);
}

TEST(VideoDecoderTest, OpenNullParams) {
  VideoDecoder dec;
  EXPECT_LT(dec.open(nullptr), 0);
  EXPECT_FALSE(dec.isOpen());
}

TEST(VideoDecoderTest, SendPacketBeforeOpen) {
  VideoDecoder dec;
  EXPECT_LT(dec.sendPacket(nullptr), 0);
}

TEST(VideoDecoderTest, RecvFrameBeforeOpen) {
  VideoDecoder dec;
  AVFrame* frame = av_frame_alloc();
  EXPECT_LT(dec.recvFrame(frame), 0);
  av_frame_free(&frame);
}

TEST(VideoDecoderTest, DecodeOneFrame) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty());

  AVCodecParameters* params = getVideoCodecParams(path);
  ASSERT_NE(params, nullptr);

  VideoDecoder dec;
  ASSERT_EQ(dec.open(params), 0);

  // 读取原始 packet 并解码
  FFmpegDemuxer demuxer;
  ASSERT_EQ(demuxer.open(path.c_str()), 0);

  int decodedFrames = 0;
  while (decodedFrames < 3) {   // 解码至少 3 帧
    auto pkt = demuxer.readPacket();
    if (!pkt) break;

    // 只看视频流
    auto streams = demuxer.getStreams();
    int videoIdx = -1;
    for (const auto& s : streams) {
      if (s.type == MediaType::Video) { videoIdx = s.index; break; }
    }
    if (pkt->stream_index != videoIdx) continue;

    int ret = dec.sendPacket(pkt.get());
    if (ret < 0 && ret != AVERROR(EAGAIN)) break;

    // 尝试接收解码帧
    AVFrame* frame = av_frame_alloc();
    while (true) {
      ret = dec.recvFrame(frame);
      if (ret == AVERROR(EAGAIN)) break;      // 需要更多 packet
      if (ret == AVERROR_EOF || ret < 0) break;
      ++decodedFrames;
      // 验证帧有效
      EXPECT_GT(frame->width, 0);
      EXPECT_GT(frame->height, 0);
      EXPECT_GE(frame->format, 0);
      av_frame_unref(frame);
    }
    av_frame_free(&frame);
  }

  EXPECT_GT(decodedFrames, 0);

  demuxer.close();
  dec.close();
  avcodec_parameters_free(&params);
  removeTestMedia(path);
}

TEST(VideoDecoderTest, FlushResetsState) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty());

  AVCodecParameters* params = getVideoCodecParams(path);
  ASSERT_NE(params, nullptr);

  VideoDecoder dec;
  ASSERT_EQ(dec.open(params), 0);

  // 打开 demuxer, 取第一个 packet（应该是关键帧）
  FFmpegDemuxer demuxer;
  demuxer.open(path.c_str());

  // 收集所有视频 packet 到 vector
  std::vector<std::shared_ptr<AVPacket>> packets;
  while (true) {
    auto pkt = demuxer.readPacket();
    if (!pkt) break;
    auto streams = demuxer.getStreams();
    int videoIdx = -1;
    for (const auto& s : streams) {
      if (s.type == MediaType::Video) { videoIdx = s.index; break; }
    }
    if (pkt->stream_index == videoIdx) {
      packets.push_back(pkt);
    }
  }
  demuxer.close();

  ASSERT_GE(packets.size(), 2) << "need at least 2 video packets";

  // 发送第一个 packet
  ASSERT_GE(dec.sendPacket(packets[0].get()), AVERROR_EOF);

  dec.flush();

  // flush 后重新发送第一个 packet（关键帧 → 成功）
  int ret = dec.sendPacket(packets[0].get());
  EXPECT_TRUE(ret == 0 || ret == AVERROR(EAGAIN));

  dec.close();
  avcodec_parameters_free(&params);
  removeTestMedia(path);
}

TEST(VideoDecoderTest, CloseIdempotent) {
  VideoDecoder dec;
  dec.close();
  dec.close();   // 不会崩溃
  EXPECT_FALSE(dec.isOpen());
}

TEST(VideoDecoderTest, SendNullPacketForDrain) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty());

  AVCodecParameters* params = getVideoCodecParams(path);
  ASSERT_NE(params, nullptr);

  VideoDecoder dec;
  ASSERT_EQ(dec.open(params), 0);

  // send nullptr 触发 drain
  int ret = dec.sendPacket(nullptr);
  // 可能成功或 EAGAIN（解码器状态不同）
  EXPECT_TRUE(ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

  dec.close();
  avcodec_parameters_free(&params);
  removeTestMedia(path);
}

} // namespace test
} // namespace player
