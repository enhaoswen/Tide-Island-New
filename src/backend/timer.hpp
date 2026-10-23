#pragma once

#include "backend/event_handle.hpp"

#include <chrono>
#include <source_location>

namespace Timer {

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
bool wait();
}
