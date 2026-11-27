#include "backend/timer.hpp"
#include "backend/event_queue.hpp"
#include "utils/log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <unistd.h>
#include <sys/timerfd.h>

using namespace std;
using namespace std::chrono;

namespace {

int timer_fd{-1};

Timer::Detail::EventQueue timer_queue{};

void set_timer_at(steady_clock::time_point deadline) {
    const duration since_epoch      = deadline.time_since_epoch();
    const duration seconds_part     = duration_cast<seconds>(since_epoch);
    const auto     nanoseconds_part = duration_cast<nanoseconds>(since_epoch - seconds_part);
    itimerspec     spec{};

    spec.it_value.tv_sec = static_cast<time_t>(seconds_part.count());

    spec.it_value.tv_nsec =
        static_cast<long>(nanoseconds_part.count());

    if (timerfd_settime(
        timer_fd,
        TFD_TIMER_ABSTIME,
        &spec,
        nullptr
        ) == -1) {
        Log::fatal("timerfd_settime failed");
    }
}

void arm_next() {
    if (timer_fd == -1)
        return;
    if (timer_queue.empty()) {
        const itimerspec disarmed{};
        if (timerfd_settime(timer_fd, 0, &disarmed, nullptr) == -1)
            Log::fatal("timerfd_settime failed");
    }
    else {
        set_timer_at(timer_queue.next_deadline());
    }
}

}

namespace Timer {
    Timer::EventHandle push(microseconds duration, void (*callback)(), float priority, source_location location) {
        if (timer_fd == -1)
            Log::fatal("Timer is not initialized");
        if (!callback || !isfinite(priority)) {
            Log::logger(Log::Error, "{}:{}: Invalid timer callback or priority", location.file_name(), location.line());
            return {};
        }
        const auto previous = timer_queue.next_deadline();
        const auto handle   = timer_queue.push(steady_clock::now() + max(duration, microseconds::zero()), callback, priority);
        if (timer_queue.next_deadline() != previous)
            arm_next();
        return handle;
    }

    bool cancel(EventHandle event) {
        const auto previous = timer_queue.next_deadline();
        const bool removed{timer_queue.cancel(event)};
        if (removed && timer_queue.next_deadline() != previous)
            arm_next();
        return removed;
    }

    bool is_pending(EventHandle event) { return timer_queue.contains(event); }

    void clear() {
        timer_queue.clear();
        arm_next();
    }

    void shutdown() {
        timer_queue.clear();
        if (timer_fd != -1)
            close(timer_fd);
        timer_fd = -1;
    }

    void init() {
        if (timer_fd != -1)
            return;
        timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

        if (timer_fd == -1) {
            Log::fatal("timerfd_create failed");
        }
    }

    int get_timer_fd() {
        return timer_fd;
    }

    void handle_events(std::source_location) {

        uint64_t expiration_count{};

        ssize_t result{};
        do {
            result = read(timer_fd, &expiration_count, sizeof(expiration_count));
        } while (result == -1 && errno == EINTR);
        // A Wayland callback can cancel/rearm the timer after poll reported readiness.
        if (result == -1 && errno == EAGAIN)
            return;
        if (result != sizeof(expiration_count))
            Log::fatal("timerfd read failed");

        const auto now = steady_clock::now();

        while (auto callback = timer_queue.pop_ready(now))
            callback();
        arm_next();
    }

} // namespace Timer
