/// PlayerController — 完整音视频管线.
/// v3: StateMachine + FrameQueue + AVSyncEngine + SeekHandler

#include "control/player_controller/player_controller.h"
#include "core/event/event_types.h"
#include "source/demuxer/stream_info.h"
#include "utils/logger/logger.h"

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

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

PlayerController::PlayerController()
    : m_avSync(m_clockMgr)
{
    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO);
    setupStateMachine_();
    setupSeekHandler_();
}

PlayerController::PlayerController(const PlayerConfig& config)
    : m_config(config)
    , m_avSync(m_clockMgr)
{
    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO);

    setupStateMachine_();
    setupSeekHandler_();
    m_avSync.setMaster(m_config.sync.master_clock);
    m_avSync.setSyncParams(
        m_config.sync.max_frame_delay_us,
        m_config.sync.drop_threshold_us,
        m_config.sync.sync_tolerance_us);
}

PlayerController::~PlayerController()
{
    stop();
    teardown_();
    SDL_Quit(); // centralized SDL cleanup (subsystems quit by individual renderers)
}

// ── setupStateMachine_ ──────────────────────────────────────────────────

void PlayerController::setupStateMachine_()
{
    m_stateMachine.addListener([this](PlayerState old, PlayerState ns) {
        if (m_callback && old != ns) {
            m_callback->onStateChanged(old, ns);
        }
    });
}

// ── setupSeekHandler_ ────────────────────────────────────────────────────

void PlayerController::setupSeekHandler_()
{
    SeekHandler::Dependencies deps;
    deps.clockMgr = &m_clockMgr;

    deps.demuxSeekTo = [this](int64_t posMs, int /*flags*/) -> bool {
        return m_demuxer.seekTo(posMs) >= 0;
    };
    deps.flushAudioDecoder = [this]() { m_audioDecoder.flush(); };
    deps.flushVideoDecoder = [this]() { m_videoDecoder.flush(); };

    m_seekHandler = std::make_unique<SeekHandler>(deps);
}

// ── open ─────────────────────────────────────────────────────────────────

Result<void> PlayerController::open(const char* url)
{
    return open(url, m_config);
}

Result<void> PlayerController::open(const char* url, const PlayerConfig& config)
{
    if (!url || !*url) return {ErrorCode::InvalidArg, "URL is empty"};

    stop();
    teardown_();
    m_config = config;

    // Add to playlist (first item auto-selects if playlist was empty)
    m_playlist.add(url);

    if (!m_stateMachine.transit(EventType::Open)) {
        return {ErrorCode::Unknown, "Not in Idle state"};
    }

    int ret = initPipeline_(m_playlist.current().c_str());
    if (ret < 0) {
        notifyError_("Failed to open media");
        m_stateMachine.transit(EventType::Error);
        return {ErrorCode::OpenFailed, "Failed to open media"};
    }

    m_stateMachine.transit(EventType::MediaLoaded);
    return {};
}

// ── play / pause / stop ──────────────────────────────────────────────────

Result<void> PlayerController::play()
{
    auto s = m_stateMachine.getState();
    if (s == PlayerState::Ready || s == PlayerState::Paused) {
        if (s == PlayerState::Paused) {
            m_audioRenderer.resume();
            m_clockMgr.setPaused(false);
        } else {
            m_clockMgr.audioClock()->setClock(0.0);
            m_clockMgr.setPaused(false);
            m_playStart = std::chrono::steady_clock::now();
            LOGD_CLOCK("play: start_time=0ms audio_clock=0.0");
            if (m_audioStreamIdx >= 0) m_audioRenderer.resume();
            startThreads_();
        }
        m_stateMachine.transit(EventType::Play);
        if (m_callback) m_callback->onPlay();
        return {};
    }
    if (s == PlayerState::Completed && m_loop) {
        m_audioEOS.store(false, std::memory_order_release);
        m_videoEOS.store(false, std::memory_order_release);
        m_abortRequested.store(false, std::memory_order_release);
        m_demuxer.seekTo(0);
        m_audioDecoder.flush();
        m_videoDecoder.flush();
        if (m_videoFrmQueue) m_videoFrmQueue->flush();
        if (m_audioPktQueue) m_audioPktQueue->flush();
        if (m_videoPktQueue) m_videoPktQueue->flush();
        m_clockMgr.audioClock()->setClock(0.0);
        m_clockMgr.setPaused(false);
        m_playStart = std::chrono::steady_clock::now();
        if (m_audioStreamIdx >= 0) m_audioRenderer.resume();
        startThreads_();
        m_stateMachine.transit(EventType::Play);
        if (m_callback) m_callback->onPlay();
        return {};
    }
    return {ErrorCode::Unknown, "Cannot play in current state"};
}

Result<void> PlayerController::pause()
{
    if (m_stateMachine.getState() != PlayerState::Playing) {
        return {ErrorCode::Unknown, "Not playing"};
    }
    m_audioRenderer.pause();
    m_clockMgr.setPaused(true);
    m_stateMachine.transit(EventType::Pause);
    if (m_callback) m_callback->onPause();
    return {};
}

Result<void> PlayerController::stop()
{
    stopThreads_();
    m_audioRenderer.destroy();
    m_videoRenderer.destroy();
    m_stateMachine.transit(EventType::Stop);
    m_stateMachine.transit(EventType::Stopped);
    if (m_callback) m_callback->onStopped();
    return {};
}

Result<void> PlayerController::seek(int64_t posMs)
{
    auto s = m_stateMachine.getState();
    if (s != PlayerState::Playing && s != PlayerState::Paused) {
        return {ErrorCode::Unknown, "Can only seek while playing or paused"};
    }
    if (!m_seekHandler->seekTo(posMs)) {
        return {ErrorCode::Unknown, "Seek failed"};
    }
    m_stateMachine.transit(EventType::Seek);
    return {};
}

Result<void> PlayerController::setSpeed(double speed)
{
    if (speed < 0.5 || speed > 2.0)
        return {ErrorCode::InvalidArg, "Speed must be 0.5~2.0"};
    m_speed = speed;
    m_clockMgr.setSpeed(speed);
    return {};
}

Result<void> PlayerController::setVolume(float volume)
{
    if (volume < 0.0f || volume > 1.0f)
        return {ErrorCode::InvalidArg, "Volume must be 0.0~1.0"};
    m_volume = volume;
    return {};
}

Result<void> PlayerController::setLoop(bool loop)
{
    m_loop = loop;
    m_playlist.setLoop(loop ? LoopMode::LoopAll : LoopMode::NoLoop);
    return {};
}

// ── 查询 ─────────────────────────────────────────────────────────────────

PlayerState    PlayerController::getState()    const { return m_stateMachine.getState(); }
Result<int64_t> PlayerController::getPosition() const { return static_cast<int64_t>(m_clockMgr.masterTime() * 1000.0); }
Result<int64_t> PlayerController::getDuration() const { return m_durationMs; }
bool           PlayerController::isPlaying()   const { return getState() == PlayerState::Playing; }
bool           PlayerController::isSeeking()   const { return m_seekHandler->isSeeking(); }
void           PlayerController::setCallback(IPlayerCallback* cb) { m_callback = cb; }

// ── initPipeline_ ─────────────────────────────────────────────────────────

int PlayerController::initPipeline_(const char* url)
{
    int ret = m_demuxer.open(url);
    if (ret < 0) return ret;

    auto streams = m_demuxer.getStreams();
    m_audioStreamIdx = m_videoStreamIdx = -1;

    for (const auto& s : streams) {
        if (s.type == MediaType::Audio && m_audioStreamIdx < 0) {
            m_audioStreamIdx = s.index;
        }
        if (s.type == MediaType::Video && m_videoStreamIdx < 0) {
            m_videoStreamIdx = s.index;
            m_durationMs = (s.duration > 0) ? s.duration / 1000 : 0;
        }
    }

    // ── Audio pipeline ──────────────────────────────────────────────────
    if (m_audioStreamIdx >= 0) {
        AVStream* as = m_demuxer.formatContext()->streams[m_audioStreamIdx];
        ret = m_audioDecoder.open(as->codecpar);
        if (ret < 0) return ret;

        int ch = as->codecpar->ch_layout.nb_channels;
        if (ch <= 0) ch = 2;
        int sr = as->codecpar->sample_rate;
        if (sr <= 0) sr = 44100;

        // Wire SDL audio clock → ClockManager for real-time AV sync
        m_audioRenderer.setClockTarget(m_clockMgr.audioClock());

        RenderConfig rcfg;
        m_audioRenderer.init(rcfg);

        // Eagerly open audio device BEFORE video window creation.
        // On Linux/PulseAudio, SDL_CreateWindow can interfere with
        // subsequent audio device opening. Opening audio first avoids this.
        int outRate  = m_config.render.audio.sample_rate;
        int outCh    = m_config.render.audio.channels;
        int outFmt   = m_config.render.audio.format;
        m_audioRenderer.openDevice(
            outRate, outCh, static_cast<AVSampleFormat>(outFmt));
        // outRate may have been changed by SDL to the device's actual rate

        m_audioResampler.init(
            sr,
            static_cast<int>(as->codecpar->format),
            ch,
            outRate,   // use device's actual sample rate
            outFmt,
            outCh);

        m_audioPktQueue = std::make_shared<PktQueue>(
            static_cast<size_t>(m_config.misc.audio_pkt_q_size));
    }

    // ── Video pipeline ──────────────────────────────────────────────────
    if (m_videoStreamIdx >= 0) {
        AVStream* vs = m_demuxer.formatContext()->streams[m_videoStreamIdx];
        ret = m_videoDecoder.open(vs->codecpar);
        if (ret < 0) {
            m_videoStreamIdx = -1;
        } else {
            AVRational tb = vs->avg_frame_rate;
            if (tb.num > 0 && tb.den > 0) {
                m_frameDuration = av_q2d(av_inv_q(tb));
            }

            m_videoPktQueue = std::make_shared<PktQueue>(
                static_cast<size_t>(m_config.misc.video_pkt_q_size));
            m_videoFrmQueue = std::make_shared<FrmQueue>(16); // big enough to absorb bursts

            RenderConfig vcfg;
            vcfg.width  = vs->codecpar->width;
            vcfg.height = vs->codecpar->height;
            vcfg.title  = "Player SDK";
            ret = m_videoRenderer.init(vcfg);
            if (ret < 0) {
                m_videoStreamIdx = -1;
                fprintf(stderr, "[warn] Video renderer init failed, audio-only mode\n");
            }
        }
    }

    // Update SeekHandler with actual queue pointers (queues don't exist at ctor time)
    {
        SeekHandler::Dependencies deps;
        deps.clockMgr = &m_clockMgr;
        deps.demuxSeekTo = [this](int64_t posMs, int) -> bool {
            return m_demuxer.seekTo(posMs) >= 0;
        };
        deps.flushAudioDecoder = [this]() { m_audioDecoder.flush(); };
        deps.flushVideoDecoder = [this]() { m_videoDecoder.flush(); };
        deps.videoPktQueue = m_videoPktQueue;
        deps.audioPktQueue = m_audioPktQueue;
        deps.videoFrmQueue = m_videoFrmQueue;
        m_seekHandler = std::make_unique<SeekHandler>(deps);
    }

    return 0;
}

// ── 线程管理 ─────────────────────────────────────────────────────────────

void PlayerController::startThreads_()
{
    m_abortRequested.store(false);

    m_demuxThread = std::make_unique<std::thread>(
        &PlayerController::demuxLoop_, this);

    if (m_audioStreamIdx >= 0) {
        m_audioDecodeThread = std::make_unique<std::thread>(
            &PlayerController::audioDecodeLoop_, this);
    }

    if (m_videoStreamIdx >= 0) {
        m_videoDecodeThread = std::make_unique<std::thread>(
            &PlayerController::videoDecodeLoop_, this);
        // Video rendering happens on main thread via pumpEvents()
    }
}

void PlayerController::stopThreads_()
{
    m_abortRequested.store(true);

    if (m_audioPktQueue) m_audioPktQueue->abort();
    if (m_videoPktQueue) m_videoPktQueue->abort();

    auto join = [&](auto& t) {
        if (t && t->joinable()) { t->join(); t.reset(); }
    };
    join(m_demuxThread);
    join(m_audioDecodeThread);
    join(m_videoDecodeThread);
}

void PlayerController::teardown_()
{
    m_demuxer.close();
    m_audioDecoder.close();
    m_videoDecoder.close();
    m_audioResampler.close();
    m_audioPktQueue.reset();
    m_videoPktQueue.reset();
    m_videoFrmQueue.reset();
    m_audioEOS.store(false, std::memory_order_release);
    m_videoEOS.store(false, std::memory_order_release);
    m_audioStreamIdx = m_videoStreamIdx = -1;
    m_durationMs = 0;
}

// ── demuxLoop_ ───────────────────────────────────────────────────────────

void PlayerController::demuxLoop_()
{
    // ── 丢帧恢复状态 ─────────────────────────────────────────────────────
    // 当视频 PacketQueue 满且 push 超时，说明解码/渲染管线跟不上。
    // 此时不能简单丢包：H.264/H.265 的 P/B 帧依赖前面的参考帧，
    // 丢掉一个 P 帧会导致后续所有帧直到下一个 I 帧都无法解码（GOP 断裂）。
    //
    // 策略：进入 "跳过模式"，丢弃所有非关键帧，直到遇到下一个关键帧，
    //       并向解码器注入 flush 以清除残留的参考帧状态。
    //       这样解码器从关键帧"干净启动"，画面最多跳一下，不会持续花屏。
    m_dropVideoUntilKeyframe = false;

    while (!m_abortRequested.load(std::memory_order_acquire)) {
        if (m_seekHandler->isSeeking()) {
            std::this_thread::sleep_for(2ms);
            // Seek 后解码器会被 flush，丢帧状态自动失效。
            m_dropVideoUntilKeyframe = false;
            continue;
        }

        auto pkt = m_demuxer.readPacket();
        if (!pkt) {
            break;  // EOS — 用 nullptr sentinel 通知解码器
        }

        // ── 音频路径 ──────────────────────────────────────────────────────
        if (pkt->stream_index == m_audioStreamIdx && m_audioPktQueue) {
            // 音频帧间无依赖，丢一包只影响 ~20ms。
            // 用无限阻塞等待：Decoder 消费后 condvar 自然唤醒。
            // abort() 会中断阻塞，保证 stop() 能正常退出。
            if (!m_audioPktQueue->push(pkt, -1)) {
                // push 返回 false = 队列被 abort → 退出
                break;
            }
        }
        // ── 视频路径 ──────────────────────────────────────────────────────
        else if (pkt->stream_index == m_videoStreamIdx && m_videoPktQueue) {
            bool isKeyframe = (pkt->flags & AV_PKT_FLAG_KEY);

            if (isKeyframe) {
                m_dropVideoUntilKeyframe = false;
                if (!m_videoPktQueue->push(pkt, -1)) break;
            } else if (m_dropVideoUntilKeyframe) {
                continue;
            } else {
                if (!m_videoPktQueue->push(pkt, 100)) {
                    m_dropVideoUntilKeyframe = true;

                    // 向视频解码器注入 flush，清除残留的 GOP 参考帧状态。
                    // flush() 会在队列中插入 flush token（nullptr），
                    // videoDecodeLoop_ 读到后调用 m_videoDecoder.flush()。
                    m_videoPktQueue->flush();

                    // 当前 packet (非关键帧) 已被丢弃，shared_ptr 自动释放。
                    continue;
                }
            }
        }
    }

    // EOS sentinel: 通知解码器流结束。用无限阻塞确保送达。
    if (m_audioPktQueue) m_audioPktQueue->push(nullptr);
    if (m_videoPktQueue) m_videoPktQueue->push(nullptr);
}

// ── audioDecodeLoop_ ─────────────────────────────────────────────────────

void PlayerController::audioDecodeLoop_()
{
    AVFrame* f = av_frame_alloc();
    if (!f) return;

    while (!m_abortRequested.load(std::memory_order_acquire)) {
        auto opt = m_audioPktQueue->pop(100);
        if (!opt.has_value()) continue;

        auto pkt = *opt;
        if (!pkt) {
            // EOF sentinel — flush decoder and exit thread
            m_audioDecoder.flush();
            m_audioEOS.store(true, std::memory_order_release);
            break;
        }

        if (m_seekHandler->isSeeking()) continue;

        int send_ret = m_audioDecoder.sendPacket(pkt.get());
        if (send_ret < 0 && send_ret != AVERROR(EAGAIN)) continue;

        while (true) {
            int ret = m_audioDecoder.recvFrame(f);
            if (ret == AVERROR(EAGAIN)) {
                break;
            }
            if (ret < 0) {
                break;
            }

            AVFrame* rf = m_audioResampler.convert(f);
            if (rf) {
                ++m_logCntAudio;
                if (m_logCntAudio <= 5 || m_logCntAudio % 100 == 0) {
                    using namespace std::chrono;
                    auto e = duration<double, std::milli>(steady_clock::now() - m_playStart).count();
                    double aPts = f->pts * av_q2d(m_demuxer.formatContext()
                        ->streams[m_audioStreamIdx]->time_base);
                    LOGD_AUDIO("#%d T+%.0fms pts=%.1fms samp=%d",
                        m_logCntAudio, e, aPts * 1000.0, rf->nb_samples);
                }
                m_audioRenderer.render(rf);
                av_frame_free(&rf);
            }
            av_frame_unref(f);
        }
    }

    av_frame_free(&f);
}

// ── videoDecodeLoop_ ─────────────────────────────────────────────────────

void PlayerController::videoDecodeLoop_()
{
    AVFrame* workFrame = av_frame_alloc();
    if (!workFrame) return;

    // Cache time_base for PTS conversion
    AVRational timeBase{1, 25};
    if (m_demuxer.formatContext() && m_videoStreamIdx >= 0) {
        timeBase = m_demuxer.formatContext()->streams[m_videoStreamIdx]->time_base;
    }
    double tbSec = av_q2d(timeBase);

    while (!m_abortRequested.load(std::memory_order_acquire)) {
        if (m_seekHandler->isSeeking()) {
            std::this_thread::sleep_for(2ms);
            continue;
        }

        auto opt = m_videoPktQueue->pop(100);
        if (!opt.has_value()) continue;

        auto pkt = *opt;
        if (!pkt) {
            // EOF sentinel — flush decoder and exit thread
            m_videoDecoder.flush();
            m_videoEOS.store(true, std::memory_order_release);
            break;
        }

        // Skip packets more than 2s ahead of audio clock.
        double pktPts = pkt->pts * av_q2d(m_demuxer.formatContext()
            ->streams[m_videoStreamIdx]->time_base);
        double aClk = m_clockMgr.audioClock()->getClock();
        if (pktPts - aClk > 2.0) {
            if (++m_logCntSkip <= 5) LOGD_VIDEO("skip pkt pts=%.1fms clk=%.1fms (>2s ahead)", pktPts*1000, aClk*1000);
            continue;
        }

        int send_ret = m_videoDecoder.sendPacket(pkt.get());
        if (send_ret < 0 && send_ret != AVERROR(EAGAIN)) continue;

        while (true) {
            int ret = m_videoDecoder.recvFrame(workFrame);
            if (ret == AVERROR(EAGAIN)) {
                break;
            }
            if (ret < 0) {
                break;
            }

            // Transfer ownership of workFrame to a Frame object.
            // Frame takes a shared_ptr with custom deleter that will
            // call av_frame_unref + av_frame_free.
            auto frame = std::make_shared<Frame>(workFrame, MediaType::Video);

            // Override PTS with correct time_base conversion
            if (workFrame->pts != int64_t(AV_NOPTS_VALUE)) {
                frame->pts = workFrame->pts * tbSec;
            }

            // Spin until FrameQueue has space. With 16-frame capacity and
            // nextFrame called BEFORE syncVideo, blocking should be ~1ms.
            ++m_logCntVideo;
            using namespace std::chrono;
            auto vDecodeTime = steady_clock::now();
            while (!m_videoFrmQueue->pushFrame(frame)) {
                if (m_abortRequested.load(std::memory_order_acquire)) break;
                std::this_thread::sleep_for(microseconds(200));
            }
            auto vBlocked = duration<double, std::milli>(steady_clock::now() - vDecodeTime).count();
            if (m_logCntVideo <= 10 || m_logCntVideo % 50 == 0) {
                auto e = duration<double, std::milli>(steady_clock::now() - m_playStart).count();
                LOGD_VIDEO("#%d T+%.0fms pts=%.1fms fq=%zu blocked=%.1fms",
                    m_logCntVideo, e, frame->pts * 1000.0, m_videoFrmQueue->size(), vBlocked);
            }

            // Allocate a fresh workFrame for the next decode iteration
            workFrame = av_frame_alloc();
            if (!workFrame) break;
        }
    }

    av_frame_free(&workFrame);
}

// ── pumpEvents ────────────────────────────────────────────────────────────

bool PlayerController::pumpEvents()
{
    // Poll SDL events on main thread (X11 requirement)
    if (m_videoStreamIdx >= 0) {
        if (!m_videoRenderer.pollEvents()) {
            return false; // window closed
        }
    }

    // EOS detection: all decode threads reached EOF and render queues drained.
    bool audioEOS = (m_audioStreamIdx < 0) ||
        m_audioEOS.load(std::memory_order_acquire);
    bool videoEOS = (m_videoStreamIdx < 0) ||
        m_videoEOS.load(std::memory_order_acquire);
    bool queueDrained = !m_videoFrmQueue || m_videoFrmQueue->empty();
    bool hasContent = (m_audioStreamIdx >= 0 || m_videoStreamIdx >= 0);

    if (audioEOS && videoEOS && queueDrained && hasContent &&
        m_stateMachine.getState() == PlayerState::Playing) {
        // Try next track in playlist
        int64_t nextIdx = m_playlist.next();
        if (nextIdx >= 0) {
            // Play next track
            LOGD_RENDER("EOS — switching to playlist track %lld", (long long)nextIdx);
            if (m_callback) m_callback->onCompletion();

            teardown_();
            m_audioEOS.store(false); m_videoEOS.store(false);
            m_abortRequested.store(false);
            m_clockMgr.audioClock()->setClock(0.0);
            m_clockMgr.setPaused(false);
            m_playStart = std::chrono::steady_clock::now();

            int ret = initPipeline_(m_playlist.current().c_str());
            if (ret < 0) {
                m_stateMachine.transit(EventType::EOS);
                if (m_callback) m_callback->onCompletion();
                return true;
            }
            startThreads_();
            return true;
        }

        // No more tracks — real end
        m_stateMachine.transit(EventType::EOS);
        LOGD_RENDER("EOS detected — transitioning to Completed");
        if (m_callback) m_callback->onCompletion();
        return true;
    }

    // Render one video frame, synced to audio clock (master)
    if (m_videoStreamIdx >= 0 && m_videoFrmQueue) {
        std::shared_ptr<Frame> newFrame;
        if (m_videoFrmQueue->peekFrame(newFrame)) {
            // Advance queue IMMEDIATELY (before syncVideo may sleep).
            // This frees a slot for the decode thread now, not 100ms later.
            m_videoFrmQueue->nextFrame();

            auto action = m_avSync.syncVideo(newFrame);

            ++m_logCntRender;
            using namespace std::chrono;
            auto e = duration<double, std::milli>(steady_clock::now() - m_playStart).count();
            double aClk = m_clockMgr.audioClock()->getClock();
            double diffMs = (newFrame->pts - aClk) * 1000.0;

            const char* actStr = (action == AVSyncEngine::SyncAction::Drop) ? "DROP" : "RENDER";

            if (action == AVSyncEngine::SyncAction::Drop || m_logCntRender <= 15 || m_logCntRender % 50 == 0)
                LOGD_AV("#%d T+%.0fms pts=%.1fms clk=%.1fms diff=%+.1fms -> %s R=%d D=%d fq=%zu",
                    m_logCntRender, e, newFrame->pts * 1000.0, aClk * 1000.0,
                    diffMs, actStr, m_logCntRender - m_logCntDrop, m_logCntDrop, m_videoFrmQueue->numRemaining());

            if (action == AVSyncEngine::SyncAction::Drop) {
                ++m_logCntDrop;
            } else {
                AVFrame* avf = newFrame->frame.get();
                if (avf && avf->data[0]) {
                    m_videoRenderer.render(avf);
                    notifyProgress_();
                }
            }
        }
    }

    return true;
}

// ── 事件/回调 ────────────────────────────────────────────────────────────

void PlayerController::notifyError_(const char* msg)
{
    m_eventBus.post(PlayerEvent(EventType::Error, msg));
    if (m_callback) {
        m_callback->onError(ErrorCode::Unknown, msg);
    }
}

void PlayerController::notifyProgress_()
{
    if (m_callback) {
        auto pos = getPosition();
        m_callback->onProgress(pos.valueOr(0), m_durationMs);
    }
}

} // namespace player
