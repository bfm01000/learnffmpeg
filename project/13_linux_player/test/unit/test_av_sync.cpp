#include "control/av_sync_engine/av_sync_engine.h"
#include "core/clock/clock_manager.h"
#include "core/memory/frame.h"

#include <gtest/gtest.h>

extern "C" {
#include <libavutil/frame.h>
}

namespace player {
namespace test {

class AVSyncTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_clockMgr.audioClock()->setClock(0.0);
        m_clockMgr.setMasterSource(MasterClockSource::Audio);
        m_avSync = std::make_unique<AVSyncEngine>(m_clockMgr);
    }

    std::shared_ptr<Frame> makeFrame(double pts)
    {
        AVFrame* avf = av_frame_alloc();
        avf->pts = static_cast<int64_t>(pts * 1000); // fake PTS in ms
        avf->time_base = AVRational{1, 1000};         // ms time base
        avf->width = 320;
        avf->height = 240;
        avf->format = 0; // YUV420P
        avf->data[0] = reinterpret_cast<uint8_t*>(0x1); // non-null sentinel
        return std::make_shared<Frame>(avf, MediaType::Video);
    }

    ClockManager m_clockMgr;
    std::unique_ptr<AVSyncEngine> m_avSync;
};

TEST_F(AVSyncTest, NullFrameDrops)
{
    auto action = m_avSync->syncVideo(nullptr);
    EXPECT_EQ(action, AVSyncEngine::SyncAction::Drop);
}

TEST_F(AVSyncTest, FrameWithinToleranceRenders)
{
    // Audio clock at 0.0, frame PTS at 0.005 (5ms diff)
    auto frame = makeFrame(0.005);
    auto action = m_avSync->syncVideo(frame);
    EXPECT_EQ(action, AVSyncEngine::SyncAction::Render);
}

TEST_F(AVSyncTest, FrameAheadSleepsThenRenders)
{
    // Audio clock at 0.0, frame PTS at 0.05 (50ms ahead)
    m_avSync->setSyncParams(100000, -100000, 10000); // 10ms tolerance
    auto frame = makeFrame(0.05);
    auto action = m_avSync->syncVideo(frame);
    // Should sleep briefly then render
    EXPECT_EQ(action, AVSyncEngine::SyncAction::Render);
}

TEST_F(AVSyncTest, FrameFarBehindDrops)
{
    // Audio clock at 1.0, frame PTS at 0.5 (500ms behind, exceeds -100ms threshold)
    m_clockMgr.audioClock()->setClock(1.0);
    auto frame = makeFrame(0.5);
    auto action = m_avSync->syncVideo(frame);
    EXPECT_EQ(action, AVSyncEngine::SyncAction::Drop);
}

TEST_F(AVSyncTest, CalcDelayReturnsMicroseconds)
{
    // PTS at 0.5, master at 0.4 → diff = 0.1s = 100,000us
    int64_t delay = m_avSync->calcDelay(0.5, 0.4);
    EXPECT_NEAR(delay, 100000, 1); // ±1us for floating-point rounding
}

TEST_F(AVSyncTest, SyncAudioUpdatesClock)
{
    auto frame = makeFrame(0.123);
    m_avSync->syncAudio(frame);
    double clockVal = m_clockMgr.audioClock()->getClock();
    EXPECT_NEAR(clockVal, 0.123, 0.001);
}

TEST_F(AVSyncTest, SyncAudioNullFrameDoesNotCrash)
{
    EXPECT_NO_THROW(m_avSync->syncAudio(nullptr));
}

TEST_F(AVSyncTest, SetMasterSwitchesSource)
{
    m_avSync->setMaster(MasterClockSource::System);
    EXPECT_EQ(m_avSync->getMasterSource(), MasterClockSource::System);
    EXPECT_EQ(m_clockMgr.getMasterSource(), MasterClockSource::System);
}

TEST_F(AVSyncTest, SetSyncParamsPersists)
{
    m_avSync->setSyncParams(200000, -50000, 5000);
    EXPECT_EQ(m_avSync->getMaxDelayUs(), 200000);
    EXPECT_EQ(m_avSync->getDropThresholdUs(), -50000);
    EXPECT_EQ(m_avSync->getToleranceUs(), 5000);
}

} // namespace test
} // namespace player
