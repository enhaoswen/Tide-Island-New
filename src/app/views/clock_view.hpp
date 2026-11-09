#pragma once

#include "scene/context.hpp"

#include <source_location>

namespace ClockView {

void start(Object::Handle root, FontHandle font, std::source_location l = std::source_location::current());

void stop();

void shutdown();

}
