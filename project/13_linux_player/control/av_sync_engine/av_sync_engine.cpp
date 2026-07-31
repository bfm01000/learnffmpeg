#include "av_sync_engine.h"

#include "core/memory/frame.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>

namespace player {

AVSyncEngine::AVSyncEngine(ClockManager& clock_manager)
    : clock_manager_(clock_manager)
    , master_source_(MasterClockSource::Audio)
    , max_delay_us_(100000)         // 100ms — max time to wait for a frame
    , drop_threshold_us_(-100000)   // -100ms — frame is too late, drop it
    , tolerance_us_(10000)          // 10ms — close enough, render now
{
}

AVSyncEngine::SyncAction AVSyncEngine::syncVideo(std::shared_ptr<Frame> frame)
{
    // ── Null / invalid frame → drop ────────────────────────────────────
    if (!frame || !frame->isValid()) {
        return SyncAction::Drop;
    }

    // ── Get master clock ───────────────────────────────────────────────
    const Clock* master = clock_manager_.getMaster();
    if (!master) {
        return SyncAction::Render;  // no clock → just render
    }

    double master_time = master->getClock();           // seconds
    double frame_pts   = frame->pts;                   // seconds
    double diff_sec    = frame_pts - master_time;
    int64_t diff_us    = static_cast<int64_t>(diff_sec * 1'000'000.0);

    // ── Decision ───────────────────────────────────────────────────────
    //
    //   diff_us > 0  →  frame is in the future  →  wait
    //   diff_us < 0  →  frame is in the past    →  may drop
    //
    if (diff_us < drop_threshold_us_) {
        // Frame is significantly behind master clock — drop it.
        return SyncAction::Drop;
    }

    if (std::abs(diff_us) <= tolerance_us_) {
        // Within tolerance — render immediately.
        return SyncAction::Render;
    }

    if (diff_us > tolerance_us_) {
        // Frame is ahead of clock — sleep until it's time.
        // Cap the sleep to max_delay_us to prevent indefinite stalling.
        int64_t sleep_us = std::min(diff_us - tolerance_us_ / 2, max_delay_us_);
        if (sleep_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
        }
        return SyncAction::Render;
    }

    // diff_us < -tolerance_us_ but > drop_threshold_us_
    // Slightly behind — render anyway rather than dropping.
    return SyncAction::Render;
}

void AVSyncEngine::syncAudio(std::shared_ptr<Frame> frame)
{
    if (!frame || !frame->isValid()) {
        return;
    }

    Clock* audio_clock = clock_manager_.audioClock();
    if (audio_clock) {
        audio_clock->setClock(frame->pts);
    }
}

int64_t AVSyncEngine::calcDelay(double pts, double master_clock) const
{
    double diff_sec = pts - master_clock;
    return static_cast<int64_t>(diff_sec * 1'000'000.0);
}

void AVSyncEngine::setMaster(MasterClockSource source)
{
    master_source_ = source;
    clock_manager_.setMasterSource(source);
}

void AVSyncEngine::setSyncParams(int64_t max_delay_us, int64_t drop_threshold_us, int64_t tolerance_us)
{
    max_delay_us_      = max_delay_us;
    drop_threshold_us_ = drop_threshold_us;
    tolerance_us_      = tolerance_us;
}

} // namespace player
