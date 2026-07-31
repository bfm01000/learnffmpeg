#include "control/state_machine/state_machine.h"
#include "core/event/event_types.h"
#include "api/player_types.h"

#include <gtest/gtest.h>

namespace player {
namespace test {

TEST(StateMachineTest, InitialStateIsIdle)
{
    StateMachine sm;
    EXPECT_EQ(sm.getState(), PlayerState::Idle);
}

TEST(StateMachineTest, IdleToLoadingOnOpen)
{
    StateMachine sm;
    bool ok = sm.transit(EventType::Open);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sm.getState(), PlayerState::Loading);
}

TEST(StateMachineTest, LoadingToReadyOnMediaLoaded)
{
    StateMachine sm;
    sm.transit(EventType::Open);
    bool ok = sm.transit(EventType::MediaLoaded);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sm.getState(), PlayerState::Ready);
}

TEST(StateMachineTest, InvalidTransitionReturnsFalse)
{
    StateMachine sm;
    // Can't play from Idle
    bool ok = sm.transit(EventType::Play);
    EXPECT_FALSE(ok);
    EXPECT_EQ(sm.getState(), PlayerState::Idle);
}

TEST(StateMachineTest, ReadyToPlayingOnPlay)
{
    StateMachine sm;
    sm.transit(EventType::Open);
    sm.transit(EventType::MediaLoaded);
    bool ok = sm.transit(EventType::Play);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sm.getState(), PlayerState::Playing);
}

TEST(StateMachineTest, PlayingToPausedOnPause)
{
    StateMachine sm;
    sm.transit(EventType::Open);
    sm.transit(EventType::MediaLoaded);
    sm.transit(EventType::Play);
    bool ok = sm.transit(EventType::Pause);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sm.getState(), PlayerState::Paused);
}

TEST(StateMachineTest, PausedToPlayingResume)
{
    StateMachine sm;
    sm.transit(EventType::Open);
    sm.transit(EventType::MediaLoaded);
    sm.transit(EventType::Play);
    sm.transit(EventType::Pause);
    bool ok = sm.transit(EventType::Play);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sm.getState(), PlayerState::Playing);
}

TEST(StateMachineTest, PlayingToStoppedViaPause)
{
    StateMachine sm;
    sm.transit(EventType::Open);
    sm.transit(EventType::MediaLoaded);
    sm.transit(EventType::Play);
    sm.transit(EventType::Pause);
    bool ok = sm.transit(EventType::Stop);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sm.getState(), PlayerState::Stopping);
}

TEST(StateMachineTest, StoppingToIdle)
{
    StateMachine sm;
    sm.transit(EventType::Open);
    sm.transit(EventType::MediaLoaded);
    sm.transit(EventType::Play);
    sm.transit(EventType::Pause);
    sm.transit(EventType::Stop);
    bool ok = sm.transit(EventType::Stopped);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sm.getState(), PlayerState::Idle);
}

TEST(StateMachineTest, EOSTransitionsToCompleted)
{
    StateMachine sm;
    sm.transit(EventType::Open);
    sm.transit(EventType::MediaLoaded);
    sm.transit(EventType::Play);
    bool ok = sm.transit(EventType::EOS);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sm.getState(), PlayerState::Completed);
}

TEST(StateMachineTest, ErrorAlwaysTransitionsToError)
{
    StateMachine sm;
    sm.transit(EventType::Open);
    sm.transit(EventType::MediaLoaded);
    sm.transit(EventType::Play);
    bool ok = sm.transit(EventType::Error);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sm.getState(), PlayerState::Error);
}

TEST(StateMachineTest, ErrorRetryBackToLoading)
{
    StateMachine sm;
    sm.transit(EventType::Open);
    sm.transit(EventType::MediaLoaded);
    sm.transit(EventType::Play);
    sm.transit(EventType::Error);
    bool ok = sm.transit(EventType::Retry);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sm.getState(), PlayerState::Loading);
}

TEST(StateMachineTest, StateListenerNotified)
{
    StateMachine sm;
    PlayerState oldSeen = PlayerState::Idle;
    PlayerState newSeen = PlayerState::Idle;
    int callCount = 0;

    sm.addListener([&](PlayerState o, PlayerState n) {
        oldSeen = o;
        newSeen = n;
        ++callCount;
    });

    sm.transit(EventType::Open);
    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(oldSeen, PlayerState::Idle);
    EXPECT_EQ(newSeen, PlayerState::Loading);
}

TEST(StateMachineTest, ResetReturnsToIdle)
{
    StateMachine sm;
    sm.transit(EventType::Open);
    sm.transit(EventType::MediaLoaded);
    EXPECT_EQ(sm.getState(), PlayerState::Ready);
    sm.reset();
    EXPECT_EQ(sm.getState(), PlayerState::Idle);
}

TEST(StateMachineTest, CanTransitPredictsValidity)
{
    StateMachine sm;
    EXPECT_TRUE(sm.canTransit(EventType::Open));
    EXPECT_FALSE(sm.canTransit(EventType::Play)); // not yet!
    sm.transit(EventType::Open);
    EXPECT_TRUE(sm.canTransit(EventType::MediaLoaded));
}

} // namespace test
} // namespace player
