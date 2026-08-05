#include <csignal>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/field_trials.h"
#include "api/make_ref_counted.h"
#include "examples/low_latency_sender/peer_connection_app.h"
#include "examples/low_latency_sender/websocket_signaling_client.h"
#include "rtc_base/ssl_adapter.h"

namespace {

std::atomic<bool> g_running{true};

void HandleSignal(int) { g_running = false; }

struct Options {
  std::string host = "127.0.0.1";
  int port = 3000;
  std::string room = "lab";
  VideoSourceConfig video;
};

int ParsePositiveInt(const std::string& text, const char* name) {
  int value = std::stoi(text);
  if (value <= 0) {
    std::cerr << name << " must be positive\n";
    std::exit(1);
  }
  return value;
}

Options ParseArgs(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto read_value = [&](const char* name) -> std::string {
      std::string prefix = std::string(name) + "=";
      if (arg.rfind(prefix, 0) == 0) return arg.substr(prefix.size());
      if (arg == name && i + 1 < argc) return argv[++i];
      return "";
    };
    std::string value;
    if (!(value = read_value("--host")).empty()) options.host = value;
    else if (!(value = read_value("--port")).empty()) options.port = ParsePositiveInt(value, "--port");
    else if (!(value = read_value("--room")).empty()) options.room = value;
    else if (!(value = read_value("--source")).empty()) options.video.source = value;
    else if (!(value = read_value("--file")).empty()) options.video.file = value;
    else if (!(value = read_value("--width")).empty()) options.video.width = ParsePositiveInt(value, "--width");
    else if (!(value = read_value("--height")).empty()) options.video.height = ParsePositiveInt(value, "--height");
    else if (!(value = read_value("--fps")).empty()) options.video.fps = ParsePositiveInt(value, "--fps");
    else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: low_latency_sender [--host 127.0.0.1] [--port 3000] [--room lab] "
                << "[--source synthetic|i420] [--file input.i420] [--width 640] [--height 480] [--fps 30]\n";
      std::exit(0);
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      std::exit(1);
    }
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  webrtc::Thread* main_thread = webrtc::ThreadManager::Instance()->WrapCurrentThread();
  if (!main_thread) return 1;

  const Options options = ParseArgs(argc, argv);
  webrtc::InitializeSSL();
  webrtc::Environment env = webrtc::CreateEnvironment(std::make_unique<webrtc::FieldTrials>(""));

  WebSocketSignalingClient signaling(options.host, options.port, options.room);
  auto app = webrtc::make_ref_counted<PeerConnectionApp>(
      env, options.room, options.video, [&](const std::string& text) { signaling.SendText(text); });
  signaling.SetMessageCallback([&](const std::string& text) { app->HandleSignalingMessage(text); });

  if (!app->Initialize()) return 1;
  if (!signaling.Connect()) return 2;

  std::cout << "low_latency_sender joined room '" << options.room << "' with source="
            << options.video.source << " " << options.video.width << "x" << options.video.height
            << "@" << options.video.fps << "fps. Open browser and join the same room.\n"
            << std::flush;
  while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(200));

  signaling.Close();
  app->Close();
  webrtc::CleanupSSL();
  return 0;
}