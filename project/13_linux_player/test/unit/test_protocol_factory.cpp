/// @file test/unit/test_protocol_factory.cpp
/// @brief ProtocolFactory 单元测试 — scheme 检测 + 协议创建/注册.

#include "source/protocol/protocol_factory.h"
#include "source/protocol/file_protocol/file_protocol.h"

#include <gtest/gtest.h>

namespace player {
namespace test {

// ── detectScheme ──────────────────────────────────────────────────────────

TEST(ProtocolFactoryTest, DetectFileScheme) {
  EXPECT_EQ(ProtocolFactory::detectScheme("file:///home/video.mp4"), "file");
}

TEST(ProtocolFactoryTest, DetectHttpScheme) {
  EXPECT_EQ(ProtocolFactory::detectScheme("http://example.com/video.mp4"), "http");
  EXPECT_EQ(ProtocolFactory::detectScheme("https://example.com/video.mp4"), "https");
}

TEST(ProtocolFactoryTest, DetectRtmpScheme) {
  EXPECT_EQ(ProtocolFactory::detectScheme("rtmp://live.example.com/stream"), "rtmp");
  EXPECT_EQ(ProtocolFactory::detectScheme("rtsp://192.168.1.1/live"), "rtsp");
}

TEST(ProtocolFactoryTest, DetectNoSchemeDefaultsToFile) {
  EXPECT_EQ(ProtocolFactory::detectScheme("/home/user/video.mp4"), "file");
  EXPECT_EQ(ProtocolFactory::detectScheme("./relative/path.mkv"), "file");
  EXPECT_EQ(ProtocolFactory::detectScheme("video.flv"), "file");
}

TEST(ProtocolFactoryTest, DetectEmptyUrl) {
  EXPECT_TRUE(ProtocolFactory::detectScheme("").empty());
}

// ── registerProtocol / createProtocol ─────────────────────────────────────

TEST(ProtocolFactoryTest, RegisterAndCreate) {
  // 先手动注册 FileProtocol
  ProtocolFactory::registerProtocol("file", [] {
    return std::make_unique<FileProtocol>();
  });

  auto proto = ProtocolFactory::createProtocol("file:///tmp/test.mp4");
  ASSERT_NE(proto, nullptr);
  EXPECT_EQ(proto->getSchemes().size(), 2);  // {"file", ""}
}

TEST(ProtocolFactoryTest, CreateUnknownSchemeReturnsNull) {
  auto proto = ProtocolFactory::createProtocol("unknown://foo/bar");
  EXPECT_EQ(proto, nullptr);
}

TEST(ProtocolFactoryTest, CreateEmptyUrlReturnsNull) {
  auto proto = ProtocolFactory::createProtocol("");
  EXPECT_EQ(proto, nullptr);
}

// ── registerBuiltins ──────────────────────────────────────────────────────

TEST(ProtocolFactoryTest, RegisterBuiltinsCreatesFileProtocol) {
  ProtocolFactory::registerBuiltins();

  // 裸路径 → detectScheme="file" → FileProtocol
  auto proto = ProtocolFactory::createProtocol("/some/local/file.mp4");
  ASSERT_NE(proto, nullptr);
  // 验证是 FileProtocol（通过 getSchemes）
  auto schemes = proto->getSchemes();
  bool hasFile = false;
  for (const auto& s : schemes) {
    if (s == "file") hasFile = true;
  }
  EXPECT_TRUE(hasFile);
}

// ── registerProtocol 边界 ─────────────────────────────────────────────────

TEST(ProtocolFactoryTest, RegisterEmptySchemeIgnored) {
  EXPECT_FALSE(ProtocolFactory::registerProtocol("", [] {
    return std::make_unique<FileProtocol>();
  }));
}

TEST(ProtocolFactoryTest, RegisterNullFactoryIgnored) {
  EXPECT_FALSE(ProtocolFactory::registerProtocol("test", nullptr));
}

// 永远接受任何 URL 的假协议（用于测试注册/覆盖）
namespace {
class YesProtocol : public IProtocolHandler {
public:
  bool canHandle(const char*) override { return true; }
  int  open(const char*) override { return 0; }
  int  read(uint8_t*, int) override { return -1; }
  int64_t seek(int64_t, int) override { return -1; }
  int  close() override { return 0; }
  std::vector<std::string> getSchemes() const override { return {"yes"}; }
};
} // namespace

TEST(ProtocolFactoryTest, RegisterOverwritesExisting) {
  int callCount = 0;
  ProtocolFactory::registerProtocol("override_test", [&callCount] {
    ++callCount;
    return std::make_unique<YesProtocol>();
  });

  // 第一次创建
  auto p1 = ProtocolFactory::createProtocol("override_test://a");
  EXPECT_NE(p1, nullptr);
  EXPECT_EQ(callCount, 1);

  // 覆盖注册
  ProtocolFactory::registerProtocol("override_test", [&callCount] {
    ++callCount;
    return std::make_unique<YesProtocol>();
  });

  // 第二次创建 — 使用新工厂
  auto p2 = ProtocolFactory::createProtocol("override_test://b");
  EXPECT_NE(p2, nullptr);
  EXPECT_EQ(callCount, 2);
}

// ── createProtocol + canHandle 二次验证 ──────────────────────────────────

namespace {

// 一个声明自己能处理 http，但 canHandle 拒绝所有 URL 的协议（用于测试二次验证）
class PickyProtocol : public IProtocolHandler {
public:
  bool canHandle(const char* /*url*/) override { return false; }
  int  open(const char*) override { return -1; }
  int  read(uint8_t*, int) override { return -1; }
  int64_t seek(int64_t, int) override { return -1; }
  int  close() override { return 0; }
  std::vector<std::string> getSchemes() const override { return {"picky"}; }
};

} // namespace

TEST(ProtocolFactoryTest, CreateProtocolChecksCanHandle) {
  ProtocolFactory::registerProtocol("picky", [] {
    return std::make_unique<PickyProtocol>();
  });

  // PickyProtocol::canHandle 返回 false → createProtocol 返回 nullptr
  auto proto = ProtocolFactory::createProtocol("picky://test");
  EXPECT_EQ(proto, nullptr);
}

} // namespace test
} // namespace player
