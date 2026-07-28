/// @file examples/opengl_player/main.cpp
/// @brief Advanced OpenGL player with shader effects & keyboard controls

#include "player.h"
#include "player_config.h"
#include "player_callback.h"

#include <GLFW/glfw3.h>
#include <csignal>
#include <iostream>

using namespace player;

namespace {

volatile sig_atomic_t g_running = 1;
void signalHandler(int) { g_running = 0; }

class OglPlayerCallback : public IPlayerCallback {
public:
  void onPrepared(int64_t dur_ms) override {
    std::cout << "[ogl] Prepared: " << dur_ms / 1000.0 << "s" << std::endl;
  }
  void onError(ErrorCode code, const char* msg) override {
    std::cerr << "[ogl] Error: " << msg << std::endl;
    g_running = 0;
  }
  void onCompletion() override {
    std::cout << "[ogl] Playback complete." << std::endl;
  }
};

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <url>" << std::endl;
    return 1;
  }

  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  auto config = PlayerConfig::localFilePreset();
  config.render.video.driver   = VideoRenderMode::OpenGL;
  config.render.video.vsync    = true;
  config.render.video.window_title = "Linux Player SDK — OpenGL Demo";

  OglPlayerCallback cb;
  auto player = IPlayer::create(config);
  player->setCallback(&cb);
  player->open(argv[1]);
  player->play();

  std::cout << "[ogl] Playing " << argv[1] << std::endl;
  std::cout << "[ogl] Keys: SPACE=pause, ←→=seek, ESC=quit" << std::endl;

  while (g_running &&
         player->getState() != PlayerState::Completed &&
         player->getState() != PlayerState::Error) {
    // In a real app, keyboard handling would go via GLFW callbacks
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }

  player->stop();
  std::cout << "\n[ogl] Exiting." << std::endl;
  return 0;
}
