#include "av_sync_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace player {

AVSyncEngine::AVSyncEngine(ClockManager& clock_manager)
    : clock_manager_(clock_manager)
    , master_source_(MasterClockSource::Audio)
    , max_delay_us_(100000)         // 100ms
    , drop_threshold_us_(-100000)   // -100ms
    , tolerance_us_(10000)          // 10ms
{
}

AVSyncEngine::SyncAction AVSyncEngine::syncVideo(std::shared_ptr<Frame> frame)
{
    // TODO: Implement full video sync logic:
    //   1. Get master clock value
    //   2. Calculate delay = frame->pts - master_clock
    //   3. If delay > max_delay_us -> Render (can't wait that long, just render now)
    //   4. If delay < drop_threshold_us -> Drop (too far behind)
    //   5. Otherwise -> Sleep(delay) then Render
    //   6. Update video clock after rendering

    if (!frame) {
        return SyncAction::Drop;
    }

    // TODO: Check frame validity when core/memory/frame.h MediaType conflict is resolved
    Clock* master = clock_manager_.getMaster();
    if (!master) {
        return SyncAction::Render;
    }

    // Stub: always render immediately
    // TODO: Replace with delay calculation and conditional Drop/Sleep
    return SyncAction::Render;
}

void AVSyncEngine::syncAudio(std::shared_ptr<Frame> frame)
{
    // TODO: Implement full audio sync logic:
    //   1. Update audio clock with current frame PTS
    //   2. Handle audio buffer underrun/overrun
    //   3. Adjust playback speed for drift correction (if needed)

    if (!frame) {
        return;
    }

    // TODO: Set audio clock pts = frame->pts
    // clock_manager_.getAudioClock()->setClock(frame->pts);
}

int64_t AVSyncEngine::calcDelay(double pts, double master_clock) const
{
    // TODO: Consider playback speed and frame duration
    // diff = (pts - master_clock) * 1000000 (convert to microseconds)
    double diff_sec = pts - master_clock;
    return static_cast<int64_t>(diff_sec * 1000000.0);
}

void AVSyncEngine::setMaster(MasterClockSource source)
{
    master_source_ = source;

    // Map MasterClockSource to ClockManager::MasterClockType
    switch (source) {
        case MasterClockSource::Audio:
            clock_manager_.setMaster(MasterClockType::Audio);
            break;
        case MasterClockSource::System:
            clock_manager_.setMaster(MasterClockType::Video);
            break;
        case MasterClockSource::External:
            clock_manager_.setMaster(MasterClockType::External);
            break;
    }
}

void AVSyncEngine::setSyncParams(int64_t max_delay_us, int64_t drop_threshold_us, int64_t tolerance_us)
{
    max_delay_us_      = max_delay_us;
    drop_threshold_us_ = drop_threshold_us;
    tolerance_us_      = tolerance_us;
}

} // namespace player
