/// @file examples/offscreen_player/main.cpp
/// @brief Offscreen player — outputs decoded video/audio frames via callback

#include "player.h"
#include "player_config.h"
#include "player_callback.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>

using namespace player;

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <url>" << std::endl;
    return 1;
  }

  PlayerConfig config = PlayerConfig::localFilePreset();
  config.render.video.driver = VideoRenderMode::Offscreen;
  config.render.audio.driver = AudioRenderMode::Offscreen;

  int video_frame_count = 0;
  int audio_frame_count = 0;

  auto player = IPlayer::create(config);

  class OffscreenCallback : public IPlayerCallback {
  public:
    int& video_cnt; int& audio_cnt;
    OffscreenCallback(int& v, int& a) : video_cnt(v), audio_cnt(a) {}

    void onVideoFrame(const uint8_t* data[4], const int linesize[4],
                      int w, int h, int fmt, int64_t pts_us) override {
      video_cnt++;
      // e.g. encode, analyze, or feed to an external pipeline
    }
    void onAudioFrame(const uint8_t* data, int samples,
                      int sr, int ch, int fmt) override {
      audio_cnt++;
    }
    void onError(ErrorCode code, const char* msg) override {
      std::cerr << "Error: " << msg << std::endl;
    }
  } cb(video_frame_count, audio_frame_count);

  player->setCallback(&cb);

  if (player->open(argv[1]) != 0) {
    std::cerr << "Failed to open" << std::endl;
    return 1;
  }

  player->play();

  // Play for 10 seconds
  std::this_thread::sleep_for(std::chrono::seconds(10));

  player->stop();

  std::cout << "Video frames: " << video_frame_count
            << ", Audio frames: " << audio_frame_count << std::endl;
  return 0;
}
