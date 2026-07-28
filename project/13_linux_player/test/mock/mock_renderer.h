#pragma once

#include "render/i_renderer.h"
#include <gmock/gmock.h>

namespace player {
namespace test {

class MockRenderer : public IRenderer {
public:
  MOCK_METHOD(int, init, (const RenderConfig& cfg), (override));
  MOCK_METHOD(int, render, (AVFrame* frame), (override));
  MOCK_METHOD(void, resize, (int w, int h), (override));
  MOCK_METHOD(void, destroy, (), (override));
};

} // namespace test
} // namespace player
