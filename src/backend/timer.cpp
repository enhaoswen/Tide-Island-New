#include "backend/timer.hpp"
#include "command.hpp"
#include "backend/event_queue.hpp"
#include "backend/media.hpp"
#include "backend/wayland.hpp"
#include "utils/log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <poll.h>
#include <span>
#include <unistd.h>
#include <vector>
#include <sys/timerfd.h>

using namespace std;
using namespace std::chrono;

namespace {

int timer_fd{-1};
int wayland_fd{-1};
// Cleared once stdin hits EOF / hangs up (e.g. started with stdin = /dev/null),
// otherwise poll would report it readable forever and the loop would spin.
bool stdin_open{true};

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

bool Timer::wait(bool animating) {
    const short wayland_events = Wayland::prepare_events();

    constexpr size_t kFixed = 3;
    static vector<pollfd> fds;
    fds.clear();
    fds.push_back({.fd = wayland_fd, .events = wayland_events, .revents = 0});
    fds.push_back({.fd = timer_fd, .events = POLLIN, .revents = 0});
    // A negative fd is ignored by poll and keeps the slot layout fixed.
    fds.push_back({.fd = stdin_open ? STDIN_FILENO : -1, .events = POLLIN, .revents = 0});
    Media::append_poll_fds(fds);

    int timeout_ms{animating ? 16 : -1};
    const auto media_deadline = Media::next_deadline();
    if (media_deadline) {
        const auto left = ceil<milliseconds>(*media_deadline - steady_clock::now()).count();
        const int  ms   = static_cast<int>(std::clamp<long long>(left, 0, numeric_limits<int>::max()));
        timeout_ms      = timeout_ms < 0 ? ms : min(timeout_ms, ms);
    }

    int result{};
    do {
        result = poll(fds.data(), fds.size(), timeout_ms);
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

    // Media reports through its listener; the view requests a redraw only for
    // what it shows, so e.g. lyric line ticks while collapsed draw nothing.
    {
        const span<const pollfd> media_fds{fds.begin() + kFixed, fds.end()};
        const bool ready = any_of(media_fds.begin(), media_fds.end(), [](const pollfd& p) { return p.revents != 0; });
        if (ready || (media_deadline && steady_clock::now() >= *media_deadline))
            Media::process(media_fds);
    }

    if (fds[1].revents & POLLIN) {
        handle_events();
        return true;
    }

    if (fds[2].revents & (POLLIN | POLLHUP | POLLERR)) {
        string cmd{};
        if (getline(cin, cmd))
            Command::handle_command(cmd);
        else
            stdin_open = false;
    }

    const bool wayland_redraw{Wayland::get_wayland_redraw()};
    // A timeout only means "next animation frame" or a media deadline; the
    // latter does not need a frame by itself.
    return wayland_redraw || animating;
}
