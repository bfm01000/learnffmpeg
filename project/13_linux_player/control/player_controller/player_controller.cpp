/// @file player_controller.cpp
/// @brief PlayerController — 播放器中央控制器. v1: 音频优先.

#include "control/player_controller/player_controller.h"

#include "core/event/event_types.h"
#include "source/demuxer/stream_info.h"

#include <cerrno>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
}

using namespace std::chrono_literals;

namespace player {

// ── 生命周期 ────────────────────────────────────────────────────────────

PlayerController::PlayerController() {
  changeState_(PlayerState::Idle);
}

PlayerController::PlayerController(const PlayerConfig& config)
  : m_config(config) {
  changeState_(PlayerState::Idle);
}

PlayerController::~PlayerController() {
  stop();
  teardown_();
}

// ── open ─────────────────────────────────────────────────────────────────

int PlayerController::open(const char* url) {
  return open(url, m_config);
}

int PlayerController::open(const char* url, const PlayerConfig& config) {
  if (!url || !*url) return -EINVAL;

  stop();
  teardown_();
  m_config = config;

  changeState_(PlayerState::Loading);

  int ret = initPipeline_(url);
  if (ret < 0) {
    notifyError_("Failed to open media");
    changeState_(PlayerState::Error);
    return ret;
  }

  changeState_(PlayerState::Ready);
  return 0;
}

// ── play / pause / stop ──────────────────────────────────────────────────

int PlayerController::play() {
  if (m_state.load() == PlayerState::Ready ||
      m_state.load() == PlayerState::Paused) {
    startThreads_();
    changeState_(PlayerState::Playing);
    return 0;
  }
  return -1;
}

int PlayerController::pause() {
  if (m_state.load() == PlayerState::Playing) {
    m_audioRenderer.pause();
    changeState_(PlayerState::Paused);
    return 0;
  }
  return -1;
}

int PlayerController::stop() {
  stopThreads_();
  m_audioRenderer.destroy();
  changeState_(PlayerState::Idle);
  return 0;
}

// ── seek ────────────────────────────────────────────────────────────────

int PlayerController::seek(int64_t positionMs) {
  if (m_state.load() != PlayerState::Playing &&
      m_state.load() != PlayerState::Paused) {
    return -1;
  }
  m_seeking.store(true);

  // 1. 暂停时钟
  m_clockMgr.setPaused(true);

  // 2. 清空队列
  m_audioPktQueue->flush();

  // 3. Flush 解码器
  m_audioDecoder.flush();

  // 4. Demuxer seek
  m_demuxer.seekTo(positionMs);

  // 5. 恢复
  m_clockMgr.audioClock()->setClock(static_cast<double>(positionMs) / 1000.0);
  m_clockMgr.setPaused(false);
  m_seeking.store(false);

  return 0;
}

// ── setSpeed / setVolume / setLoop ──────────────────────────────────────

int PlayerController::setSpeed(double speed) { return -1; /* TODO */ }
int PlayerController::setVolume(float volume) { return -1; /* TODO */ }
int PlayerController::setLoop(bool loop) { return -1; /* TODO */ }

// ── 查询 ────────────────────────────────────────────────────────────────

PlayerState PlayerController::getState()    const { return m_state.load(); }
int64_t     PlayerController::getPosition() const {
  return static_cast<int64_t>(m_clockMgr.masterTime() * 1000.0);
}
int64_t     PlayerController::getDuration() const { return m_durationMs; }
bool        PlayerController::isPlaying()   const { return getState() == PlayerState::Playing; }
bool        PlayerController::isSeeking()   const { return m_seeking.load(); }
void        PlayerController::setCallback(IPlayerCallback* cb) { m_callback = cb; }

// ── initPipeline_ ──────────────────────────────────────────────────────

int PlayerController::initPipeline_(const char* url) {
  // 1. 打开 Demuxer
  int ret = m_demuxer.open(url);
  if (ret < 0) return ret;

  m_durationMs = 0;
  auto streams = m_demuxer.getStreams();
  m_audioStreamIdx = -1;
  m_videoStreamIdx = -1;

  for (const auto& s : streams) {
    if (s.type == MediaType::Audio && m_audioStreamIdx < 0) {
      m_audioStreamIdx = s.index;
    }
    if (s.type == MediaType::Video && m_videoStreamIdx < 0) {
      m_videoStreamIdx = s.index;
      m_durationMs = s.duration > 0 ? s.duration / 1000 : 0;
    }
  }

  // 至少要有音频流
  if (m_audioStreamIdx < 0) return -1;

  // 2. 创建音频解码器
  AVStream* audioSt = m_demuxer.formatContext()->streams[m_audioStreamIdx];
  ret = m_audioDecoder.open(audioSt->codecpar);
  if (ret < 0) return ret;

  // 3. 创建音频重采样器 (→ S16, SDL2 格式)
  int inChannels = audioSt->codecpar->ch_layout.nb_channels;
  if (inChannels <= 0) inChannels = 2;  // fallback
  int inSampleRate = audioSt->codecpar->sample_rate;
  if (inSampleRate <= 0) inSampleRate = 44100;
  ret = m_audioResampler.init(
      inSampleRate,
      static_cast<int>(audioSt->codecpar->format),
      inChannels,
      48000, AV_SAMPLE_FMT_S16, 2);
  if (ret < 0) return ret;

  // 4. 初始化音频渲染器 (延迟打开 SDL 设备)
  RenderConfig renderCfg;
  m_audioRenderer.init(renderCfg);

  // 5. 创建 PacketQueue
  m_audioPktQueue = std::make_shared<PktQueue>(
      static_cast<size_t>(m_config.misc.audio_pkt_q_size));

  return 0;
}

// ── 线程管理 ────────────────────────────────────────────────────────────

void PlayerController::startThreads_() {
  m_abortRequested.store(false);

  m_demuxThread = std::make_unique<std::thread>(
      &PlayerController::demuxLoop_, this);

  m_audioDecodeThread = std::make_unique<std::thread>(
      &PlayerController::audioDecodeLoop_, this);
}

void PlayerController::stopThreads_() {
  m_abortRequested.store(true);

  if (m_audioPktQueue) m_audioPktQueue->abort();

  if (m_demuxThread && m_demuxThread->joinable()) {
    m_demuxThread->join();
    m_demuxThread.reset();
  }
  if (m_audioDecodeThread && m_audioDecodeThread->joinable()) {
    m_audioDecodeThread->join();
    m_audioDecodeThread.reset();
  }
}

void PlayerController::teardown_() {
  m_demuxer.close();
  m_audioDecoder.close();
  m_audioResampler.close();
  m_audioPktQueue.reset();
  m_audioStreamIdx = -1;
  m_videoStreamIdx = -1;
  m_durationMs = 0;
}

// ── demuxLoop_ ──────────────────────────────────────────────────────────

void PlayerController::demuxLoop_() {
  while (!m_abortRequested.load(std::memory_order_acquire)) {
    if (m_seeking.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(10ms);
      continue;
    }

    auto pkt = m_demuxer.readPacket();
    if (!pkt) break;   // EOF or error

    if (pkt->stream_index == m_audioStreamIdx) {
      m_audioPktQueue->push(pkt, 100);   // 100ms 超时
    }
    // video packets → discard (TODO: route to video queue)
  }

  // EOF: push nullptr to signal drain
  m_audioPktQueue->push(nullptr);
}

// ── audioDecodeLoop_ ────────────────────────────────────────────────────

void PlayerController::audioDecodeLoop_() {
  AVFrame* frame = av_frame_alloc();
  if (!frame) return;

  // 尝试跳过重采样器——直接输出原始 PCM 是否可行
  bool resampleOk = m_audioResampler.isOpen();

  while (!m_abortRequested.load(std::memory_order_acquire)) {
    auto pkt = m_audioPktQueue->pop(100);
    if (!pkt.has_value()) continue;  // 超时, 重试
    auto pktPtr = *pkt;

    // flush token (nullptr shared_ptr)
    if (!pktPtr) {
      m_audioDecoder.flush();
      continue;
    }

    if (m_seeking.load(std::memory_order_acquire)) continue;

    // 发送 packet
    int ret = m_audioDecoder.sendPacket(pktPtr.get());
    if (ret < 0 && ret != AVERROR(EAGAIN)) continue;

    // 接收解码帧
    while (true) {
      ret = m_audioDecoder.recvFrame(frame);
      if (ret == AVERROR(EAGAIN)) break;
      if (ret < 0) break;

      // v1: 跳过重采样, 直接输出解码帧到 SDL renderer
      // （AudioResampler::convert 在 .so 中有链接问题, 待 fix）
      m_audioRenderer.render(frame);

      // 更新音频时钟
      if (frame->pts != AV_NOPTS_VALUE) {
        double ptsSec = static_cast<double>(frame->pts) *
                        av_q2d(m_demuxer.formatContext()
                                   ->streams[m_audioStreamIdx]->time_base);
        m_clockMgr.audioClock()->setClock(ptsSec);
      }

      av_frame_unref(frame);
    }
  }

  av_frame_free(&frame);
}

// ── 状态/事件 ────────────────────────────────────────────────────────────

void PlayerController::changeState_(PlayerState newState) {
  PlayerState old = m_state.load();
  if (old == newState) return;
  m_state.store(newState);

  if (m_callback) {
    m_callback->onStateChanged(old, newState);
  }
}

void PlayerController::notifyError_(const char* msg) {
  m_eventBus.post(PlayerEvent(EventType::Error, msg));
  if (m_callback) {
    m_callback->onError(ErrorCode::Unknown, msg);
  }
}

} // namespace player
