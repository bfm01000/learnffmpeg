/// PlayerController — 完整音视频管线.

#include "control/player_controller/player_controller.h"
#include "core/event/event_types.h"
#include "source/demuxer/stream_info.h"

#include <cerrno>
#include <chrono>
#include <thread>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/time.h>
}

using namespace std::chrono_literals;

namespace player {

// ── 生命周期 ────────────────────────────────────────────────────────────

PlayerController::PlayerController() { changeState_(PlayerState::Idle); }
PlayerController::PlayerController(const PlayerConfig& config) : m_config(config) { changeState_(PlayerState::Idle); }
PlayerController::~PlayerController() { stop(); teardown_(); }

// ── open ─────────────────────────────────────────────────────────────────

int PlayerController::open(const char* url) { return open(url, m_config); }

int PlayerController::open(const char* url, const PlayerConfig& config) {
  if (!url || !*url) return -EINVAL;
  stop(); teardown_();
  m_config = config;
  changeState_(PlayerState::Loading);

  int ret = initPipeline_(url);
  if (ret < 0) { notifyError_("Failed to open media"); changeState_(PlayerState::Error); return ret; }

  changeState_(PlayerState::Ready);
  return 0;
}

// ── play / pause / stop ──────────────────────────────────────────────────

int PlayerController::play() {
  auto s = m_state.load();
  if (s != PlayerState::Ready && s != PlayerState::Paused) return -1;
  if (s == PlayerState::Paused) m_audioRenderer.resume();
  else startThreads_();
  changeState_(PlayerState::Playing);
  return 0;
}

int PlayerController::pause() {
  if (m_state.load() != PlayerState::Playing) return -1;
  m_audioRenderer.pause();
  changeState_(PlayerState::Paused);
  return 0;
}

int PlayerController::stop() {
  stopThreads_();
  m_audioRenderer.destroy();
  m_videoRenderer.destroy();
  changeState_(PlayerState::Idle);
  return 0;
}

// ── seek ────────────────────────────────────────────────────────────────

int PlayerController::seek(int64_t posMs) {
  auto s = m_state.load();
  if (s != PlayerState::Playing && s != PlayerState::Paused) return -1;
  m_seeking.store(true);
  m_audioPktQueue->flush();
  m_videoPktQueue->flush();
  m_audioDecoder.flush();
  m_videoDecoder.flush();
  m_demuxer.seekTo(posMs);
  m_clockMgr.audioClock()->setClock(static_cast<double>(posMs) / 1000.0);
  m_seeking.store(false);
  return 0;
}

int PlayerController::setSpeed(double) { return -1; }
int PlayerController::setVolume(float) { return -1; }
int PlayerController::setLoop(bool) { return -1; }

// ── 查询 ────────────────────────────────────────────────────────────────

PlayerState PlayerController::getState()    const { return m_state.load(); }
int64_t     PlayerController::getPosition() const { return static_cast<int64_t>(m_clockMgr.masterTime() * 1000.0); }
int64_t     PlayerController::getDuration() const { return m_durationMs; }
bool        PlayerController::isPlaying()   const { return getState() == PlayerState::Playing; }
bool        PlayerController::isSeeking()   const { return m_seeking.load(); }
void        PlayerController::setCallback(IPlayerCallback* cb) { m_callback = cb; }

// ── initPipeline_ ──────────────────────────────────────────────────────

int PlayerController::initPipeline_(const char* url) {
  int ret = m_demuxer.open(url);
  if (ret < 0) return ret;

  auto streams = m_demuxer.getStreams();
  m_audioStreamIdx = m_videoStreamIdx = -1;

  for (const auto& s : streams) {
    if (s.type == MediaType::Audio && m_audioStreamIdx < 0) m_audioStreamIdx = s.index;
    if (s.type == MediaType::Video && m_videoStreamIdx < 0) { m_videoStreamIdx = s.index; m_durationMs = s.duration > 0 ? s.duration / 1000 : 0; }
  }

  // Audio pipeline
  if (m_audioStreamIdx >= 0) {
    AVStream* as = m_demuxer.formatContext()->streams[m_audioStreamIdx];
    ret = m_audioDecoder.open(as->codecpar);
    if (ret < 0) return ret;
    int ch = as->codecpar->ch_layout.nb_channels; if (ch <= 0) ch = 2;
    int sr = as->codecpar->sample_rate; if (sr <= 0) sr = 44100;
    m_audioResampler.init(sr, static_cast<int>(as->codecpar->format), ch, 48000, AV_SAMPLE_FMT_S16, 2);
    m_audioPktQueue = std::make_shared<PktQueue>(static_cast<size_t>(m_config.misc.audio_pkt_q_size));
    RenderConfig rc; m_audioRenderer.init(rc);
  }

  // Video pipeline (non-fatal: video init failure → audio-only mode)
  if (m_videoStreamIdx >= 0) {
    AVStream* vs = m_demuxer.formatContext()->streams[m_videoStreamIdx];
    ret = m_videoDecoder.open(vs->codecpar);
    if (ret < 0) { m_videoStreamIdx = -1; }
    else {
      AVRational tb = vs->avg_frame_rate;
      if (tb.num > 0 && tb.den > 0) m_frameDuration = av_q2d(av_inv_q(tb));
      m_videoPktQueue = std::make_shared<PktQueue>(static_cast<size_t>(m_config.misc.video_pkt_q_size));
      RenderConfig vcfg;
      vcfg.width  = vs->codecpar->width;
      vcfg.height = vs->codecpar->height;
      vcfg.title  = "Player SDK";
      ret = m_videoRenderer.init(vcfg);
      if (ret < 0) { m_videoStreamIdx = -1; fprintf(stderr, "[warn] Video renderer init failed, audio-only mode\n"); }
    }
  }

  return 0;
}

// ── 线程管理 ────────────────────────────────────────────────────────────

void PlayerController::startThreads_() {
  m_abortRequested.store(false);
  m_demuxThread = std::make_unique<std::thread>(&PlayerController::demuxLoop_, this);
  if (m_audioStreamIdx >= 0) m_audioDecodeThread = std::make_unique<std::thread>(&PlayerController::audioDecodeLoop_, this);
  if (m_videoStreamIdx >= 0) { m_videoDecodeThread = std::make_unique<std::thread>(&PlayerController::videoDecodeLoop_, this); m_videoRenderThread = std::make_unique<std::thread>(&PlayerController::videoRenderLoop_, this); }
}

void PlayerController::stopThreads_() {
  m_abortRequested.store(true);
  if (m_audioPktQueue) m_audioPktQueue->abort();
  if (m_videoPktQueue) m_videoPktQueue->abort();
  auto join=[&](auto& t){ if(t&&t->joinable()){t->join();t.reset();} };
  join(m_demuxThread); join(m_audioDecodeThread); join(m_videoDecodeThread); join(m_videoRenderThread);
}

void PlayerController::teardown_() {
  m_demuxer.close(); m_audioDecoder.close(); m_videoDecoder.close(); m_audioResampler.close();
  m_audioPktQueue.reset(); m_videoPktQueue.reset();
  m_audioStreamIdx = m_videoStreamIdx = -1; m_durationMs = 0;
}

// ── demuxLoop_ ──────────────────────────────────────────────────────────

void PlayerController::demuxLoop_() {
  while (!m_abortRequested.load(std::memory_order_acquire)) {
    if (m_seeking.load(std::memory_order_acquire)) { std::this_thread::sleep_for(5ms); continue; }
    auto pkt = m_demuxer.readPacket();
    if (!pkt) break;
    if (pkt->stream_index == m_audioStreamIdx) m_audioPktQueue->push(pkt, 100);
    else if (pkt->stream_index == m_videoStreamIdx) m_videoPktQueue->push(pkt, 100);
  }
  if (m_audioPktQueue) m_audioPktQueue->push(nullptr);
  if (m_videoPktQueue) m_videoPktQueue->push(nullptr);
}

// ── audioDecodeLoop_ ────────────────────────────────────────────────────

void PlayerController::audioDecodeLoop_() {
  AVFrame* f = av_frame_alloc();
  while (!m_abortRequested.load(std::memory_order_acquire)) {
    auto opt = m_audioPktQueue->pop(100); if (!opt.has_value()) continue;
    auto pkt = *opt; if (!pkt) { m_audioDecoder.flush(); continue; }
    if (m_seeking.load(std::memory_order_acquire)) continue;
    if (m_audioDecoder.sendPacket(pkt.get()) < 0 && m_audioDecoder.sendPacket(pkt.get()) != AVERROR(EAGAIN)) continue;
    while (true) {
      int ret = m_audioDecoder.recvFrame(f);
      if (ret == AVERROR(EAGAIN)) break; if (ret < 0) break;
      AVFrame* rf = m_audioResampler.convert(f);
      if (rf) { m_audioRenderer.render(rf); av_frame_free(&rf); }
      av_frame_unref(f);
    }
  }
  av_frame_free(&f);
}

// ── videoDecodeLoop_ ────────────────────────────────────────────────────

void PlayerController::videoDecodeLoop_() {
  AVFrame* f = av_frame_alloc();
  while (!m_abortRequested.load(std::memory_order_acquire)) {
    auto opt = m_videoPktQueue->pop(100); if (!opt.has_value()) continue;
    auto pkt = *opt; if (!pkt) { m_videoDecoder.flush(); continue; }
    if (m_seeking.load(std::memory_order_acquire)) continue;
    if (m_videoDecoder.sendPacket(pkt.get()) < 0 && m_videoDecoder.sendPacket(pkt.get()) != AVERROR(EAGAIN)) continue;
    while (true) {
      int ret = m_videoDecoder.recvFrame(f);
      if (ret == AVERROR(EAGAIN)) break; if (ret < 0) break;
      // 保存最后一帧供渲染线程使用（简化：直接用最新帧覆盖）
      av_frame_unref(f);
    }
  }
  av_frame_free(&f);
}

// ── videoRenderLoop_ ────────────────────────────────────────────────────

void PlayerController::videoRenderLoop_() {
  AVFrame* f = av_frame_alloc();
  auto lastFrameTime = std::chrono::steady_clock::now();

  while (!m_abortRequested.load(std::memory_order_acquire)) {
    if (m_seeking.load(std::memory_order_acquire)) { std::this_thread::sleep_for(5ms); continue; }
    if (m_videoRenderer.shouldClose()) break;

    // 尝试接收解码帧
    int ret = m_videoDecoder.recvFrame(f);
    if (ret == 0) {
      // 简单帧率控制
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration<double>(now - lastFrameTime).count();
      double target = (m_config.sync.play_speed > 0) ? m_frameDuration / m_config.sync.play_speed : m_frameDuration;
      if (elapsed < target) std::this_thread::sleep_for(std::chrono::duration<double>(target - elapsed));
      lastFrameTime = std::chrono::steady_clock::now();

      m_videoRenderer.render(f);
      av_frame_unref(f);

      // 更新进度
      notifyProgress_();
    } else if (ret == AVERROR(EAGAIN)) {
      std::this_thread::sleep_for(2ms);
    } else {
      break; // EOF or error
    }
  }
  av_frame_free(&f);
}

// ── 状态/事件 ────────────────────────────────────────────────────────────

void PlayerController::changeState_(PlayerState newState) {
  PlayerState old = m_state.exchange(newState);
  if (old == newState) return;
  if (m_callback) m_callback->onStateChanged(old, newState);
}

void PlayerController::notifyError_(const char* msg) {
  m_eventBus.post(PlayerEvent(EventType::Error, msg));
  if (m_callback) m_callback->onError(ErrorCode::Unknown, msg);
}

void PlayerController::notifyProgress_() {
  if (m_callback) {
    int64_t pos = getPosition();
    int64_t dur = m_durationMs;
    m_callback->onProgress(pos, dur);
  }
}

} // namespace player
