#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <source_location>

namespace Timer {

struct EventHandle {
    std::uint32_t index{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t generation{};
    bool          operator==(const EventHandle&) const = default;
};
static_assert(sizeof(EventHandle) == 8);

void init();
// Priority orders simultaneous deadlines; it never suppresses an event.
EventHandle push(
    std::chrono::microseconds duration,
    void (*callback)(),
    float priority,
    std::source_location location = std::source_location::current()
);

bool cancel(EventHandle event);
bool is_pending(EventHandle event);
void clear();
void shutdown();

void handle_events(std::source_location location = std::source_location::current());
int get_timer_fd();
// Blocks until an event or, when `animating` is true, the next frame tick.
// Returns whether a redraw is needed.
bool wait(bool animating);
}
