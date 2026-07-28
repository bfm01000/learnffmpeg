/// @file examples/simple_player/main.cpp
/// @brief Minimal player example — opens a URL and plays with OpenGL window

#include "player.h"
#include "player_config.h"
#include "player_callback.h"
#include "player_types.h"

#include <csignal>
#include <cstdlib>
#include <iostream>

using namespace player;

namespace {

volatile sig_atomic_t g_running = 1;

void signalHandler(int) { g_running = 0; }

/// @brief Minimal callback implementation — prints key events
class DemoCallback : public IPlayerCallback {
public:
  void onPrepared(int64_t duration_ms) override {
    std::cout << "[demo] Prepared, duration=" << duration_ms << "ms" << std::endl;
  }
  void onCompletion() override {
    std::cout << "[demo] Playback completed." << std::endl;
  }
  void onError(ErrorCode code, const char* msg) override {
    std::cerr << "[demo] Error " << static_cast<int>(code) << ": " << msg << std::endl;
    g_running = 0;
  }
  void onStateChanged(PlayerState old_state, PlayerState new_state) override {
    std::cout << "[demo] State: " << static_cast<int>(old_state)
              << " → " << static_cast<int>(new_state) << std::endl;
  }
  void onProgress(int64_t pos_ms, int64_t dur_ms) override {
    if (dur_ms > 0 && pos_ms % 1000 < 50) {
      std::cout << "\r[demo] " << (pos_ms / 1000) << "s / "
                << (dur_ms / 1000) << "s" << std::flush;
    }
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

  const char* url = argv[1];

  auto config = PlayerConfig::localFilePreset();
  auto player = IPlayer::create(config);
  DemoCallback cb;
  player->setCallback(&cb);

  std::cout << "[demo] Opening: " << url << std::endl;

  if (player->open(url) != 0) {
    std::cerr << "[demo] Failed to open: " << url << std::endl;
    return 1;
  }

  player->play();

  // Main loop — keep alive until signal or playback ends
  while (g_running) {
    if (player->getState() == PlayerState::Completed ||
        player->getState() == PlayerState::Error) {
      break;
    }
    // In a real app, process SDL/GLFW events here
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  player->stop();
  std::cout << "\n[demo] Done." << std::endl;
  return 0;
}
