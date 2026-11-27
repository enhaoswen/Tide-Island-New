#pragma once

#include <source_location>

#include "scene/context.hpp"
#include "utils/struct.hpp"

namespace ClockView {

/// @brief Creates and starts the clock text on the given background.
void start(Object::Context& ctx,
	Object::Handle          bg,
	FontHandle              font,
	std::source_location    location = std::source_location::current());

/// @brief Stops the clock and destroys its text node.
void stop();

/// @brief Stops the clock and releases its listeners.
void shutdown();

}
