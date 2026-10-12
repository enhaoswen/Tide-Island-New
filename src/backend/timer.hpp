#pragma once

#include <chrono>
#include <source_location>

namespace Timer {

void init();
void push(
    std::chrono::microseconds duration,
    void (*callback)(),
    float priority,
    std::source_location location = std::source_location::current()
);

void handle_events(std::source_location location = std::source_location::current());
int get_timer_fd();
bool wait();
}
