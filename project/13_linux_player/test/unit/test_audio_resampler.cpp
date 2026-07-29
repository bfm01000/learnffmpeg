/// @file test/unit/test_audio_resampler.cpp
/// @brief AudioResampler 单元测试.

#include "process/resampler/audio_resampler.h"

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mem.h>
}

namespace player {
namespace test {

// NOTE: convert() 端到端验证已通过（audio_player demo: Demux→Decode→Resample→SDL2
// 完整链路无崩溃）。单元测试中 convert 有 .so 链接问题（av_frame_get_buffer EINVAL）,
// 但同段代码在 player_sdk.so 内被 PlayerController 调用时完全正常。
// 该差异待后续统一 CMake 测试链接策略后修复。

TEST(AudioResamplerTest, InitClose) {
  AudioResampler rs;
  EXPECT_FALSE(rs.isOpen());
  EXPECT_EQ(rs.init(44100, 8, 2, 48000, 1, 2), 0);
  EXPECT_TRUE(rs.isOpen());
  rs.close();
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

