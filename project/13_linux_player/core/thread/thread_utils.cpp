#include "thread_utils.h"

#include <sched.h>

namespace player {

void setThreadName(const std::string& name) {
    // Linux prctl limit is 16 bytes including null terminator
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
}

bool setThreadPriority(int policy, int priority) {
    return setThreadPriority(pthread_self(), policy, priority);
}

bool setThreadPriority(pthread_t thread, int policy, int priority) {
    struct sched_param param;
    param.sched_priority = priority;
    int ret = pthread_setschedparam(thread, policy, &param);
    return ret == 0;
}

bool setRealtimePriority(int priority) {
    return setThreadPriority(SCHED_FIFO, priority);
}

void threadYield() {
    pthread_yield();
}

} // namespace player
