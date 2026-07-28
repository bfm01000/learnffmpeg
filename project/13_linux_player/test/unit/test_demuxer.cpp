/// @file test/unit/test_demuxer.cpp
/// @brief FFmpegDemuxer 单元测试 — 需要 FFmpeg CLI 生成测试文件.

#include "source/demuxer/ffmpeg_demuxer.h"
#include "source/demuxer/stream_info.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>

namespace player {
namespace test {

// ── 测试辅助 ─────────────────────────────────────────────────────────────

/// 用 ffmpeg CLI 生成一段 1 秒的测试 MP4（testsrc + sine 音轨）.
/// @return 生成的文件路径, 失败返回空字符串.
static std::string createTestMedia() {
  std::string path = "/tmp/test_player_demuxer_" + std::to_string(getpid()) + ".mp4";

  // 1 秒彩色测试画面 + 440Hz sine 音频, libx264 + aac
  std::string cmd =
    "ffmpeg -y -hide_banner -loglevel error "
    "-f lavfi -i testsrc=duration=1:size=320x240:rate=25 "
    "-f lavfi -i sine=frequency=440:duration=1 "
    "-c:v libx264 -preset ultrafast -pix_fmt yuv420p "
    "-c:a aac -shortest "
    "\"" + path + "\" 2>/dev/null";

  int ret = std::system(cmd.c_str());
  if (ret != 0) return {};
  return path;
}

static void removeTestMedia(const std::string& path) {
  if (!path.empty()) std::remove(path.c_str());
}

// ── open / close ─────────────────────────────────────────────────────────

TEST(FFmpegDemuxerTest, OpenValidFile) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty()) << "ffmpeg CLI required to generate test media";

  FFmpegDemuxer demuxer;
  EXPECT_EQ(demuxer.open(path.c_str()), 0);
  EXPECT_NE(demuxer.formatContext(), nullptr);
  EXPECT_GT(demuxer.streamCount(), 0);

  demuxer.close();
  EXPECT_EQ(demuxer.formatContext(), nullptr);
  removeTestMedia(path);
}

TEST(FFmpegDemuxerTest, OpenNonexistentFile) {
  FFmpegDemuxer demuxer;
  int ret = demuxer.open("/tmp/player_sdk_nonexistent_demuxer_test.xyz");
  EXPECT_LT(ret, 0);
}

TEST(FFmpegDemuxerTest, OpenNullUrl) {
  FFmpegDemuxer demuxer;
  EXPECT_LT(demuxer.open(nullptr), 0);
}

TEST(FFmpegDemuxerTest, OpenEmptyUrl) {
  FFmpegDemuxer demuxer;
  EXPECT_LT(demuxer.open(""), 0);
}

// ── readPacket ───────────────────────────────────────────────────────────

TEST(FFmpegDemuxerTest, ReadPackets) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty());

  FFmpegDemuxer demuxer;
  ASSERT_EQ(demuxer.open(path.c_str()), 0);

  int pktCount = 0;
  while (true) {
    auto pkt = demuxer.readPacket();
    if (!pkt) break;   // EOF
    ++pktCount;
  }
  EXPECT_GT(pktCount, 0) << "should produce at least one packet";

  demuxer.close();
  removeTestMedia(path);
}

TEST(FFmpegDemuxerTest, ReadPacketBeforeOpen) {
  FFmpegDemuxer demuxer;
  auto pkt = demuxer.readPacket();
  EXPECT_EQ(pkt, nullptr);
}

// ── stream info ──────────────────────────────────────────────────────────

TEST(FFmpegDemuxerTest, GetStreamsAfterOpen) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty());

  FFmpegDemuxer demuxer;
  ASSERT_EQ(demuxer.open(path.c_str()), 0);

  auto streams = demuxer.getStreams();
  EXPECT_FALSE(streams.empty());

  // 至少有一个视频流
  bool hasVideo = false;
  for (const auto& s : streams) {
    if (s.type == MediaType::Video) {
      hasVideo = true;
      EXPECT_GE(s.width, 0);      // 部分编码器可能为 0
      EXPECT_GE(s.height, 0);
      EXPECT_FALSE(s.codecName.empty());
    }
  }
  EXPECT_TRUE(hasVideo);

  demuxer.close();
  removeTestMedia(path);
}

TEST(FFmpegDemuxerTest, GetStreamsBeforeOpen) {
  FFmpegDemuxer demuxer;
  auto streams = demuxer.getStreams();
  EXPECT_TRUE(streams.empty());
}

// ── seek ─────────────────────────────────────────────────────────────────

TEST(FFmpegDemuxerTest, SeekForward) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty());

  FFmpegDemuxer demuxer;
  ASSERT_EQ(demuxer.open(path.c_str()), 0);

  // Seek 到 500ms
  EXPECT_GE(demuxer.seekTo(500), 0);

  // Seek 之后应该能继续读 packet
  auto pkt = demuxer.readPacket();
  EXPECT_NE(pkt, nullptr);

  demuxer.close();
  removeTestMedia(path);
}

TEST(FFmpegDemuxerTest, SeekBeforeOpen) {
  FFmpegDemuxer demuxer;
  EXPECT_LT(demuxer.seekTo(0), 0);
}

TEST(FFmpegDemuxerTest, SeekToBeginning) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty());

  FFmpegDemuxer demuxer;
  ASSERT_EQ(demuxer.open(path.c_str()), 0);

  // 读几个 packet 再 seek 回开头
  int pkts = 0;
  while (pkts < 5) {
    auto pkt = demuxer.readPacket();
    if (!pkt) break;
    ++pkts;
  }

  EXPECT_GE(demuxer.seekTo(0), 0);

  // 应该能继续读
  auto pkt = demuxer.readPacket();
  EXPECT_NE(pkt, nullptr);

  demuxer.close();
  removeTestMedia(path);
}

// ── close idempotent ─────────────────────────────────────────────────────

TEST(FFmpegDemuxerTest, CloseIdempotent) {
  FFmpegDemuxer demuxer;
  EXPECT_EQ(demuxer.close(), 0);
  EXPECT_EQ(demuxer.close(), 0);  // 第二次 close 也不 crash
}

// ── reopen ──────────────────────────────────────────────────────────────

TEST(FFmpegDemuxerTest, ReopenNewFile) {
  std::string path = createTestMedia();
  ASSERT_FALSE(path.empty());

  FFmpegDemuxer demuxer;

  // 第一次 open
  ASSERT_EQ(demuxer.open(path.c_str()), 0);
  EXPECT_GT(demuxer.streamCount(), 0);
  demuxer.close();

  // 第二次 open（相同文件）
  ASSERT_EQ(demuxer.open(path.c_str()), 0);
  EXPECT_GT(demuxer.streamCount(), 0);

  // 能正常读
  auto pkt = demuxer.readPacket();
  EXPECT_NE(pkt, nullptr);

  demuxer.close();
  removeTestMedia(path);
}

// ── formatContext ───────────────────────────────────────────────────────

TEST(FFmpegDemuxerTest, FormatContextNullBeforeOpen) {
  FFmpegDemuxer demuxer;
  EXPECT_EQ(demuxer.formatContext(), nullptr);
  EXPECT_EQ(demuxer.streamCount(), 0);
}

} // namespace test
} // namespace player
