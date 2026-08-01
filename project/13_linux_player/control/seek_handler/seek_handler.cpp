#include "seek_handler.h"

#include "core/clock/clock_manager.h"
#include "core/queue/frame_queue.h"
#include "core/queue/packet_queue.h"

namespace player {

SeekHandler::SeekHandler(const Dependencies& deps)
    : m_deps(deps)
{
}

bool SeekHandler::seekTo(int64_t position_ms, int32_t flags)
{
    // ── Prevent concurrent seeks ───────────────────────────────────────
    if (m_seeking.exchange(true, std::memory_order_acq_rel)) {
        return false;  // already seeking
    }

    m_targetPosMs.store(position_ms, std::memory_order_release);

    // ── 1. Pause master clock ──────────────────────────────────────────
    if (m_deps.clockMgr) {
        m_deps.clockMgr->setPaused(true);
    }

    // ── 2. Flush packet & frame queues ─────────────────────────────────
    flushQueues_();

    // ── 3. Flush decoders (increments serial) ──────────────────────────
    flushDecoders_();

    // ── 4. Demuxer seek ────────────────────────────────────────────────
    bool seek_ok = false;
    if (m_deps.demuxSeekTo) {
        seek_ok = m_deps.demuxSeekTo(position_ms, flags);
    }

    if (!seek_ok) {
        // Seek failed — resume clock and report error
        if (m_deps.clockMgr) {
            m_deps.clockMgr->setPaused(false);
        }
        m_seeking.store(false, std::memory_order_release);
        return false;
    }

    // ── 5. Update clock to new position ────────────────────────────────
    if (m_deps.clockMgr) {
        double new_pts = static_cast<double>(position_ms) / 1000.0;
        m_deps.clockMgr->audioClock()->setClock(new_pts);
        m_deps.clockMgr->systemClock()->setClock(new_pts);
        m_deps.clockMgr->externalClock()->setClock(new_pts);
        m_deps.clockMgr->setPaused(false);
    }

    m_seeking.store(false, std::memory_order_release);
    return true;
}

void SeekHandler::flushQueues_()
{
    if (m_deps.videoPktQueue) {
        m_deps.videoPktQueue->flush();
        m_deps.videoPktQueue->reset();
    }
    if (m_deps.audioPktQueue) {
        m_deps.audioPktQueue->flush();
        m_deps.audioPktQueue->reset();
    }
    if (m_deps.videoFrmQueue) {
        m_deps.videoFrmQueue->flush();
    }
}

void SeekHandler::flushDecoders_()
{
    if (m_deps.flushAudioDecoder) {
        m_deps.flushAudioDecoder();
    }
    if (m_deps.flushVideoDecoder) {
        m_deps.flushVideoDecoder();
    }
}

} // namespace player
