#pragma once

#include "scene/i_used_to_have_a_dream"

#include <source_location>

namespace ClockView {

void start(Object::Handle<Object::Layout> root, FontHandle font, std::source_location l = std::source_location::current());
void stop();
void shutdown();

} // namespace ClockView
