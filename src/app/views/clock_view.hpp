#pragma once

#include "scene/object.hpp"

#include <source_location>

namespace ClockView {

void start(Object::Handle<Object::Layout> root, FontHandle font, std::source_location l = std::source_location::current());
void stop();
void shutdown();

} // namespace ClockView
