#include "state_machine.h"

#include <algorithm>
#include <cassert>

namespace player {

StateMachine::StateMachine()
    : state_(PlayerState::Idle)
{
    // ── Full transition table per ARCHITECTURE.md §5.2 ────────────────────
    //
    // format: { event, from, to, action (optional) }

    // IDLE → LOADING
    registerTransition({EventType::Open, PlayerState::Idle, PlayerState::Loading, nullptr});

    // LOADING → READY  /  LOADING → ERROR
    registerTransition({EventType::MediaLoaded, PlayerState::Loading, PlayerState::Ready, nullptr});
    registerTransition({EventType::Error,       PlayerState::Loading, PlayerState::Error, nullptr});

    // READY → PLAYING  /  READY → (self on Seek)
    registerTransition({EventType::Play, PlayerState::Ready, PlayerState::Playing, nullptr});
    registerTransition({EventType::Seek, PlayerState::Ready, PlayerState::Ready,  nullptr});

    // PLAYING → PAUSED / BUFFERING / COMPLETED / ERROR  /  (self on Seek)
    registerTransition({EventType::Play,      PlayerState::Playing, PlayerState::Playing,  nullptr}); // already playing
    registerTransition({EventType::Pause,     PlayerState::Playing, PlayerState::Paused,   nullptr});
    registerTransition({EventType::Buffering, PlayerState::Playing, PlayerState::Buffering,nullptr});
    registerTransition({EventType::EOS,       PlayerState::Playing, PlayerState::Completed,nullptr});
    registerTransition({EventType::Seek,      PlayerState::Playing, PlayerState::Playing,  nullptr});
    registerTransition({EventType::Error,     PlayerState::Playing, PlayerState::Error,    nullptr});

    // PAUSED → PLAYING / PAUSED (seek) / STOPPING
    registerTransition({EventType::Play,  PlayerState::Paused, PlayerState::Playing,  nullptr});
    registerTransition({EventType::Seek,  PlayerState::Paused, PlayerState::Paused,   nullptr});
    registerTransition({EventType::Stop,  PlayerState::Paused, PlayerState::Stopping, nullptr});
    registerTransition({EventType::Error, PlayerState::Paused, PlayerState::Error,    nullptr});

    // BUFFERING → PLAYING / ERROR / STOPPING
    registerTransition({EventType::BufferingEnd, PlayerState::Buffering, PlayerState::Playing,  nullptr});
    registerTransition({EventType::Error,        PlayerState::Buffering, PlayerState::Error,    nullptr});
    registerTransition({EventType::Stop,         PlayerState::Buffering, PlayerState::Stopping, nullptr});

    // COMPLETED → PLAYING (loop) / STOPPING
    registerTransition({EventType::Play,  PlayerState::Completed, PlayerState::Playing,  nullptr});
    registerTransition({EventType::Stop,  PlayerState::Completed, PlayerState::Stopping, nullptr});
    registerTransition({EventType::Error, PlayerState::Completed, PlayerState::Error,    nullptr});

    // ERROR → LOADING (retry) / STOPPING
    registerTransition({EventType::Retry, PlayerState::Error, PlayerState::Loading,  nullptr});
    registerTransition({EventType::Stop,  PlayerState::Error, PlayerState::Stopping, nullptr});

    // STOPPING → IDLE
    registerTransition({EventType::Stopped, PlayerState::Stopping, PlayerState::Idle, nullptr});

    // ── Global: async error from any "live" state → ERROR ─────────────────
    // These are the states from which an async error can fire.
    // Idle/Stopping/Error themselves are not eligible.
    const PlayerState liveStates[] = {
        PlayerState::Loading, PlayerState::Ready,
        PlayerState::Playing, PlayerState::Paused,
        PlayerState::Buffering, PlayerState::Completed
    };
    for (auto s : liveStates) {
        // Only register if not already covered above (Playing/Paused/Buffering
        // already have Error transitions, but registerTransition deduplicates).
        registerTransition({EventType::Error, s, PlayerState::Error, nullptr});
    }
}

bool StateMachine::transit(EventType event)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const Transition* t = findTransition(event);
    if (!t) {
        return false;
    }

    if (t->from != state_) {
        return false;
    }

    PlayerState old_state = state_;
    state_ = t->to;

    // Execute action if present
    if (t->action) {
        t->action();
    }

    // Notify listeners (outside lock? No — listeners expect state to be
    // consistent; holding the lock is correct here.)
    for (auto& listener : listeners_) {
        listener(old_state, state_);
    }

    return true;
}

bool StateMachine::canTransit(EventType event) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const Transition* t = findTransition(event);
    return (t != nullptr && t->from == state_);
}

PlayerState StateMachine::getState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void StateMachine::registerTransition(const Transition& transition)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Remove existing rules with same (event, from) pair to avoid duplicates
    auto it = std::remove_if(transitions_.begin(), transitions_.end(),
        [&](const Transition& t) {
            return t.event == transition.event && t.from == transition.from;
        });
    transitions_.erase(it, transitions_.end());

    transitions_.push_back(transition);
}

void StateMachine::addListener(StateListener listener)
{
    if (listener) {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners_.push_back(std::move(listener));
    }
}

void StateMachine::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = PlayerState::Idle;
}

const StateMachine::Transition* StateMachine::findTransition(EventType event) const
{
    for (const auto& t : transitions_) {
        if (t.event == event && t.from == state_) {
            return &t;
        }
    }
    return nullptr;
}

} // namespace player
