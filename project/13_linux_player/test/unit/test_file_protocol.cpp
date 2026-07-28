/// @file test/unit/test_file_protocol.cpp
/// @brief FileProtocol 单元测试 — 本地文件协议处理器.

#include "source/protocol/file_protocol/file_protocol.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace player {
namespace test {

// ── 测试辅助 ─────────────────────────────────────────────────────────────

/// 创建临时文件, 写入指定内容. 返回文件路径.
/// 每次调用生成唯一文件名（pid + 递增计数器）, 避免多文件测试冲突.
static std::string createTempFile(const std::string& content) {
  static int counter = 0;
  std::string path = "/tmp/test_player_fileproto_"
                   + std::to_string(getpid()) + "_"
                   + std::to_string(++counter);
  std::ofstream out(path, std::ios::binary);
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  out.close();
  return path;
}

/// 删除临时文件.
static void removeTempFile(const std::string& path) {
  std::remove(path.c_str());
}

// ── canHandle ─────────────────────────────────────────────────────────────

TEST(FileProtocolTest, CanHandleFileScheme) {
  FileProtocol proto;
  EXPECT_TRUE(proto.canHandle("file:///home/user/video.mp4"));
  EXPECT_TRUE(proto.canHandle("file:///tmp/test.avi"));
}

TEST(FileProtocolTest, CanHandleBarePath) {
  FileProtocol proto;
  EXPECT_TRUE(proto.canHandle("/home/user/video.mp4"));
  EXPECT_TRUE(proto.canHandle("./relative/path.mkv"));
  EXPECT_TRUE(proto.canHandle("video.flv"));
}

TEST(FileProtocolTest, RejectHttpUrl) {
  FileProtocol proto;
  EXPECT_FALSE(proto.canHandle("http://example.com/video.mp4"));
  EXPECT_FALSE(proto.canHandle("https://example.com/video.mp4"));
}

TEST(FileProtocolTest, RejectRtmpUrl) {
  FileProtocol proto;
  EXPECT_FALSE(proto.canHandle("rtmp://live.example.com/stream"));
  EXPECT_FALSE(proto.canHandle("rtsp://192.168.1.1/live"));
}

TEST(FileProtocolTest, RejectNullAndEmpty) {
  FileProtocol proto;
  EXPECT_FALSE(proto.canHandle(nullptr));
  EXPECT_FALSE(proto.canHandle(""));
}

// ── getSchemes ────────────────────────────────────────────────────────────

TEST(FileProtocolTest, SchemesIncludeFileAndEmpty) {
  FileProtocol proto;
  auto schemes = proto.getSchemes();
  bool hasFile  = false;
  bool hasEmpty = false;
  for (const auto& s : schemes) {
    if (s == "file") hasFile = true;
    if (s == "")    hasEmpty = true;
  }
  EXPECT_TRUE(hasFile);
  EXPECT_TRUE(hasEmpty);
}

// ── open / read / close ──────────────────────────────────────────────────

TEST(FileProtocolTest, OpenReadClose) {
  std::string content = "Hello, Player SDK!";
  std::string path    = createTempFile(content);

  FileProtocol proto;
  ASSERT_EQ(proto.open(path.c_str()), 0);

  uint8_t buf[128];
  int n = proto.read(buf, sizeof(buf));
  ASSERT_GT(n, 0);
  EXPECT_EQ(std::string(reinterpret_cast<char*>(buf), static_cast<size_t>(n)),
            content);

  EXPECT_EQ(proto.close(), 0);
  removeTempFile(path);
}

TEST(FileProtocolTest, ReadReturnsZeroAtEof) {
  std::string path = createTempFile("abc");

  FileProtocol proto;
  ASSERT_EQ(proto.open(path.c_str()), 0);

  uint8_t buf[16];
  proto.read(buf, 16);   // read all 3 bytes
  int n = proto.read(buf, 16);   // should be 0 (EOF)
  EXPECT_EQ(n, 0);

  proto.close();
  removeTempFile(path);
}

TEST(FileProtocolTest, OpenWithFilePrefix) {
  std::string content = "file:// prefix test";
  std::string path    = createTempFile(content);

  std::string url = "file://" + path;

  FileProtocol proto;
  ASSERT_EQ(proto.open(url.c_str()), 0);

  uint8_t buf[128];
  int n = proto.read(buf, sizeof(buf));
  ASSERT_GT(n, 0);

  proto.close();
  removeTempFile(path);
}

TEST(FileProtocolTest, OpenNonexistentFile) {
  FileProtocol proto;
  int ret = proto.open("/tmp/player_sdk_nonexistent_12345.xyz");
  EXPECT_LT(ret, 0);   // should fail
}

TEST(FileProtocolTest, ReadOnClosedFd) {
  FileProtocol proto;
  uint8_t buf[16];
  int ret = proto.read(buf, 16);
  EXPECT_LT(ret, 0);
}

TEST(FileProtocolTest, SeekOnClosedFd) {
  FileProtocol proto;
  int64_t ret = proto.seek(0, SEEK_SET);
  EXPECT_LT(ret, 0);
}

TEST(FileProtocolTest, CloseIdempotent) {
  FileProtocol proto;
  // close without open
  EXPECT_EQ(proto.close(), 0);
  // double close
  EXPECT_EQ(proto.close(), 0);
}

// ── seek ─────────────────────────────────────────────────────────────────

TEST(FileProtocolTest, SeekSet) {
  std::string content = "0123456789ABCDEF";
  std::string path    = createTempFile(content);

  FileProtocol proto;
  ASSERT_EQ(proto.open(path.c_str()), 0);

  // seek to offset 3 ("3")
  int64_t pos = proto.seek(3, SEEK_SET);
  EXPECT_EQ(pos, 3);

  uint8_t buf[8];
  int n = proto.read(buf, 8);
  ASSERT_GT(n, 0);
  EXPECT_EQ(buf[0], '3');
  EXPECT_EQ(buf[1], '4');

  proto.close();
  removeTempFile(path);
}

TEST(FileProtocolTest, SeekCur) {
  std::string path = createTempFile("ABCDEFGHIJ");

  FileProtocol proto;
  ASSERT_EQ(proto.open(path.c_str()), 0);

  proto.seek(2, SEEK_SET);         // → 'C'
  int64_t pos = proto.seek(3, SEEK_CUR); // → 'F'
  EXPECT_EQ(pos, 5);

  uint8_t c;
  proto.read(&c, 1);
  EXPECT_EQ(c, 'F');

  proto.close();
  removeTempFile(path);
}

TEST(FileProtocolTest, SeekEnd) {
  std::string content = "0123456789";
  std::string path    = createTempFile(content);

  FileProtocol proto;
  ASSERT_EQ(proto.open(path.c_str()), 0);

  // seek to file end - 2
  int64_t pos = proto.seek(-2, SEEK_END);
  EXPECT_EQ(pos, 8);

  uint8_t c;
  proto.read(&c, 1);
  EXPECT_EQ(c, '8');

  proto.close();
  removeTempFile(path);
}

// ── reopen ───────────────────────────────────────────────────────────────

TEST(FileProtocolTest, ReopenClearsPrevious) {
  std::string a = createTempFile("AAA");
  std::string b = createTempFile("BBB");

  FileProtocol proto;
  ASSERT_EQ(proto.open(a.c_str()), 0);

  uint8_t buf[8];
  int n = proto.read(buf, 3);
  ASSERT_EQ(n, 3);
  EXPECT_EQ(buf[0], 'A');

  // open another file → should close first
  ASSERT_EQ(proto.open(b.c_str()), 0);
  n = proto.read(buf, 3);
  ASSERT_EQ(n, 3);
  EXPECT_EQ(buf[0], 'B');

  proto.close();
  removeTempFile(a);
  removeTempFile(b);
}

} // namespace test
} // namespace player
