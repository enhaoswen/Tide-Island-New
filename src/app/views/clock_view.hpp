#pragma once

#include "scene/object.hpp"

#include <source_location>

namespace ClockView {

struct ClockDesc {
    // Borrowed resources; the caller keeps them valid while the clock is running.
    Object::Handle<Object::Layout> root{};
    FontHandle font{};
};

void start(ClockDesc clock_desc, std::source_location l = std::source_location::current());
void stop();
void shutdown();

} // namespace ClockView
