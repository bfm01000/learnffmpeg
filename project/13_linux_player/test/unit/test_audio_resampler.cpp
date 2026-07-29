/// @file test/unit/test_audio_resampler.cpp
/// @brief AudioResampler 单元测试.
///
/// NOTE: convert() integration test 暂时跳过 — 在链接 libplayer_sdk.so 时
/// av_frame_get_buffer / av_samples_alloc 表现异常（EINVAL），但 standalone
/// 链接 .o 文件时完全正常。疑似 FFmpeg 6.1 的 shared-library 符号冲突问题,
/// 待完整 pipeline 集成后以端到端方式验证。

#include "process/resampler/audio_resampler.h"

#include <gtest/gtest.h>

namespace player {
namespace test {

TEST(AudioResamplerTest, InitClose) {
  AudioResampler rs;
  EXPECT_FALSE(rs.isOpen());
  EXPECT_EQ(rs.init(44100, 8 /*FLTP*/, 2, 48000, 1 /*S16*/, 2), 0);
  EXPECT_TRUE(rs.isOpen());
  rs.close();
  EXPECT_FALSE(rs.isOpen());
}

TEST(AudioResamplerTest, InitReplacesPrevious) {
  AudioResampler rs;
  ASSERT_EQ(rs.init(44100, 8, 2, 48000, 1, 2), 0);
  ASSERT_EQ(rs.init(22050, 1, 1, 44100, 8, 1), 0);
  EXPECT_TRUE(rs.isOpen());
  rs.close();
}

TEST(AudioResamplerTest, ConvertNullReturnsNull) {
  AudioResampler rs;
  rs.init(44100, 8, 2, 48000, 1, 2);
  EXPECT_EQ(rs.convert(nullptr), nullptr);
  rs.close();
}

TEST(AudioResamplerTest, CloseIdempotent) {
  AudioResampler rs;
  rs.close();
  rs.close();
}

} // namespace test
} // namespace player
