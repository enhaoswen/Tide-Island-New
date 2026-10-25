#include "backend/timer.hpp"
#include "backend/event_queue.hpp"
#include "backend/wayland.hpp"
#include "utils/log.hpp"
#include "command.hpp"
#include "scene/object.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>

using namespace std;
using namespace std::chrono;

namespace {

int timer_fd{-1};
int wayland_fd{-1};

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
    if (timer_fd == -1) return;
    if (timer_queue.empty()) {
        const itimerspec disarmed{};
        if (timerfd_settime(timer_fd, 0, &disarmed, nullptr) == -1) Log::fatal("timerfd_settime failed");
    }
    else set_timer_at(timer_queue.next_deadline());
}

} // namespace

Timer::EventHandle Timer::push(microseconds duration, void (*callback)(), float priority, source_location location) {
    if (timer_fd == -1) Log::fatal("Timer is not initialized");
    if (!callback || !isfinite(priority)) {
        Log::logger(Log::Error, "{}:{}: Invalid timer callback or priority", location.file_name(), location.line());
        return {};
    }
    const auto previous = timer_queue.next_deadline();
    const auto handle = timer_queue.push(steady_clock::now() + max(duration, microseconds::zero()), callback, priority);
    if (timer_queue.next_deadline() != previous) arm_next();
    return handle;
}

bool Timer::cancel(EventHandle event) {
    const auto previous = timer_queue.next_deadline();
    const bool removed{timer_queue.cancel(event)};
    if (removed && timer_queue.next_deadline() != previous) arm_next();
    return removed;
}

bool Timer::is_pending(EventHandle event) { return timer_queue.contains(event); }

void Timer::clear() {
    timer_queue.clear();
    arm_next();
}

void Timer::shutdown() {
    timer_queue.clear();
    if (timer_fd != -1) close(timer_fd);
    timer_fd = wayland_fd = -1;
}

void Timer::init() {
    if (timer_fd != -1) return;
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

    if (timer_fd == -1) {
        Log::fatal("timerfd_create failed");
    }

    wayland_fd = Wayland::get_wayland_fd();

    if (wayland_fd == -1) {
        Log::fatal("Failed to get Wayland file descriptor");
    }
}

int Timer::get_timer_fd() {
    return timer_fd;
}

void Timer::handle_events(std::source_location) {

    uint64_t expiration_count{};

    ssize_t result{};
    do {
        result = read(timer_fd, &expiration_count, sizeof(expiration_count));
    } while (result == -1 && errno == EINTR);
    // A Wayland callback can cancel/rearm the timer after poll reported readiness.
    if (result == -1 && errno == EAGAIN) return;
    if (result != sizeof(expiration_count)) Log::fatal("timerfd read failed");

    const auto now = steady_clock::now();

    while (auto callback = timer_queue.pop_ready(now)) callback();
    arm_next();
}

bool Timer::wait() {
    const short wayland_events = Wayland::prepare_events();

    pollfd fds[] = {
        {
            .fd      = wayland_fd,
            .events  = wayland_events,
            .revents = 0,
        },
        {
            .fd      = timer_fd,
            .events  = POLLIN,
            .revents = 0,
        },
        {
            .fd      = STDIN_FILENO,
            .events  = POLLIN,
            .revents = 0,
        },
    };

    int result{};
    // Keep frames advancing while animations run; sleep indefinitely when idle.
    const int timeout_ms{Object::has_active_animations() ? 16 : -1};

    do {
        result = poll(fds, 3, timeout_ms);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        Wayland::cancel_events();
        Log::fatal("poll failed: {}", strerror(errno));
    }

    Wayland::handle_events(fds[0].revents);

    if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        Log::fatal(
            "Timer fd poll error: {}",
            fds[1].revents
            );
    }

    if (fds[1].revents & POLLIN) {
        handle_events();
        return true;
    }

    if (fds[2].revents & POLLIN) {
        string cmd{};
        getline(cin, cmd);
        Command::handle_command(cmd);
    }

    const bool wayland_redraw{Wayland::get_wayland_redraw()};
    return result == 0 || wayland_redraw || Object::has_active_animations();
}
