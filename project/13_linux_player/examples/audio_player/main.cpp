/// @file examples/audio_player/main.cpp
/// @brief 端到端音频播放 Demo — 验证完整音频链路.

#include "api/player.h"
#include "api/player_callback.h"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace player;
using namespace std::chrono_literals;

namespace {
volatile sig_atomic_t g_running = 1;
void onSignal(int) { g_running = 0; }
} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <media_file>" << std::endl;
    return 1;
  }

  signal(SIGINT, onSignal);
  signal(SIGTERM, onSignal);

  auto config = PlayerConfig::localFilePreset();
  config.render.audio.latency_ms = 50;

  auto player = IPlayer::create(config);

  struct Callback : IPlayerCallback {
    void onStateChanged(PlayerState old, PlayerState s) override {
      const char* names[] = {"Idle","Loading","Ready","Playing","Paused","Buffering","Completed","Error","Stopping"};
      std::cout << "[state] " << names[(int)old] << " → " << names[(int)s] << std::endl;
    }
    void onError(ErrorCode code, const char* msg) override {
      std::cerr << "[error] " << msg << std::endl;
      g_running = 0;
    }
    void onCompletion() override {
      std::cout << "[done] Playback completed." << std::endl;
      g_running = 0;
    }
    void onProgress(int64_t posMs, int64_t durMs) override {
      if (durMs > 0) {
        std::cout << "\r[ " << posMs/1000 << "s / " << durMs/1000 << "s ]" << std::flush;
      }
    }
  } cb;

  player->setCallback(&cb);

  std::cout << "Opening: " << argv[1] << std::endl;
  if (player->open(argv[1]) != 0) {
    std::cerr << "Failed to open." << std::endl;
    return 1;
  }

  std::cout << "Playing... (Ctrl+C to stop)" << std::endl;
  player->play();

  while (g_running) {
    if (player->getState() == PlayerState::Completed ||
        player->getState() == PlayerState::Error) {
      break;
    }
    std::this_thread::sleep_for(100ms);
  }

  player->stop();
  std::cout << "\nDone." << std::endl;
  return 0;
}
