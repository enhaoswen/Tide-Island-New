#pragma once

#include <source_location>

namespace ClockView {

void start(std::source_location location = std::source_location::current());
void stop();

} // namespace ClockView
