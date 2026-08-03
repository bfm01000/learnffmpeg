/// Full audio+video player demo — main thread renders video.
#include "api/player.h"
#include "api/player_callback.h"
#include "utils/logger/logger.h"

#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

using namespace player;
using namespace std::chrono_literals;

volatile sig_atomic_t g_run = 1;
void onSig(int) { g_run = 0; }

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "Usage: " << argv[0] << " <file>\n"; return 1; }
  signal(SIGINT, onSig); signal(SIGTERM, onSig);

  // Log to file alongside executable
  std::string exeDir(argv[0]);
  auto slash = exeDir.rfind('/');
  std::string logPath = (slash != std::string::npos)
      ? exeDir.substr(0, slash + 1) + "full_player.log"
      : "full_player.log";
  Logger::instance().setOutputFile(logPath);
  Logger::instance().setLevel(LogLevel::Debug);
  std::cout << "Log: " << logPath << "\n";

  auto cfg = PlayerConfig::localFilePreset();
  auto p = IPlayer::create(cfg);

  struct CB : IPlayerCallback {
    void onStateChanged(PlayerState o, PlayerState n) override {
      const char* names[] = {"Idle","Loading","Ready","Playing","Paused",
                             "Buffering","Completed","Error","Stopping"};
      std::cout << "[state] " << names[(int)o] << " -> " << names[(int)n] << "\n";
    }
    void onError(ErrorCode, const char* m) override {
      std::cerr << "[err] " << m << "\n"; g_run = 0;
    }
    void onCompletion() override {
      std::cout << "[done]\n";
    }
  } cb;

  p->setCallback(&cb);
  std::cout << "Open: " << argv[1] << "\n";
  auto r = p->open(argv[1]);
  if (r.isErr()) { std::cerr << "Failed: " << r.error().message << "\n"; return 1; }
  std::cout << "Play...\n"; p->play();

  // Main loop — pump SDL events + render frames (X11 requires main thread)
  while (g_run) {
    auto s = p->getState();
    if (s == PlayerState::Completed || s == PlayerState::Error) break;
    if (!p->pumpEvents()) break;
    std::this_thread::sleep_for(2ms);
  }

  p->stop();
  std::cout << "Done.\n";
}
