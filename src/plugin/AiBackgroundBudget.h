#pragma once
#include <algorithm>
#include <chrono>

namespace pulso::plugin::background {
inline bool mayRetryDirect(bool wasQueued, bool cancellationConfirmed,
                           std::chrono::milliseconds remaining, bool stopped) {
    return wasQueued && cancellationConfirmed && !stopped && remaining>=std::chrono::seconds(10);
}
// Queue residence consumes a separate, bounded allowance, never a fresh POST.
// All time points are monotonic; the caller may impose a stricter global cap.
struct Budget {
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;
    Clock::time_point start, last, hardDeadline;
    Duration work, queueLimit, queued{0};
    bool wasQueued;
    Budget(Clock::time_point began, Duration workBudget, Duration queueBudget,
           Clock::time_point hardCap, bool initiallyQueued)
        : start(began), last(began), hardDeadline(hardCap), work(workBudget),
          queueLimit(queueBudget), wasQueued(initiallyQueued) {}
    void observe(Clock::time_point now, bool isQueued) {
        if(wasQueued && now>last)
            queued += std::chrono::duration_cast<Duration>(now-last);
        last=now;
        wasQueued=isQueued;
    }
    Clock::time_point deadline() const {
        return std::min(hardDeadline,start+work+std::min(queued,queueLimit));
    }
    bool queueExpired() const { return queued>=queueLimit; }
};
}
