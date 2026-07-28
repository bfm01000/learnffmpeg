#pragma once

#include <pthread.h>
#include <sched.h>
#include <string>

namespace player {

// Set the name of the current thread (useful for debugging)
// On Linux this is limited to 15 characters including null terminator
// name: thread name (will be truncated to 15 chars on Linux)
void setThreadName(const std::string& name);

// Set thread scheduling priority
// policy: scheduling policy (SCHED_FIFO, SCHED_RR, SCHED_OTHER, etc.)
// priority: priority value (0 = default, valid range depends on policy)
// Returns true on success, false on failure
bool setThreadPriority(int policy, int priority);

// Set thread priority for the current thread
bool setThreadPriority(pthread_t thread, int policy, int priority);

// Set current thread to real-time FIFO priority
bool setRealtimePriority(int priority = 99);

// Yield the current thread's time slice
void threadYield();

} // namespace player
