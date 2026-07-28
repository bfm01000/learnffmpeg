#include "state_machine.h"

#include <algorithm>
#include <cassert>

namespace player {

StateMachine::StateMachine()
    : state_(PlayerState::Idle)
{
    // Register default transitions for the playback lifecycle
    // TODO: Populate full transition table based on design
}

bool StateMachine::transit(EventType event)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const Transition* t = findTransition(event);
    if (!t) {
        // No matching transition rule
        return false;
    }

    if (t->from != state_) {
        // State mismatch — rule exists but current state doesn't match
        return false;
    }

    PlayerState old_state = state_;
    state_ = t->to;

    // Execute action if present
    if (t->action) {
        t->action();
    }

    // Notify listeners
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
