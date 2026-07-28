#include "seek_handler.h"

#include "core/clock/clock_manager.h"
#include "core/queue/frame_queue.h"
#include "core/queue/packet_queue.h"

namespace player {

SeekHandler::SeekHandler(const Dependencies& deps)
    : deps_(std::make_shared<Dependencies>(deps))
    , seeking_(false)
    , target_pos_ms_(0)
{
}

bool SeekHandler::seekTo(int64_t position_ms, int32_t flags)
{
    if (seeking_.exchange(true)) {
        // Already seeking
        return false;
    }

    target_pos_ms_.store(position_ms);

    // TODO: Implement full seek flow:
    //   1. Pause the master clock (setPaused(true))
    //   2. Flush packet queues and frame queues
    //   3. Flush video decoder (increment serial)
    //   4. Flush audio decoder (increment serial)
    //   5. Call demuxer -> seekTo(position_ms, flags)
    //   6. Decode until reaching target frame
    //   7. Set clock to new position
    //   8. Resume clock (setPaused(false))
    //   9. Notify seek complete

    flushQueues();

    if (deps_->demuxer && deps_->demuxer->seekTo) {
        deps_->demuxer->seekTo(position_ms, flags);
    }

    // TODO: Reset decoder serials
    // TODO: Decode to target frame
    // TODO: Update clocks

    notifySeekComplete(position_ms);
    seeking_.store(false);

    return true;
}

void SeekHandler::flushQueues()
{
    // Flush packet queues to discard pending packets
    if (deps_->video_pkt_queue) {
        deps_->video_pkt_queue->flush();
        deps_->video_pkt_queue->reset();
    }
    if (deps_->audio_pkt_queue) {
        deps_->audio_pkt_queue->flush();
        deps_->audio_pkt_queue->reset();
    }

    // Flush frame queues to discard pending frames
    if (deps_->video_frm_queue) {
        deps_->video_frm_queue->flush();
    }
    if (deps_->audio_frm_queue) {
        deps_->audio_frm_queue->flush();
    }
}

void SeekHandler::notifySeekComplete(int64_t position_ms)
{
    // TODO: Post seek-complete event to EventBus
    // Auto placeholder for future integration
}

} // namespace player
