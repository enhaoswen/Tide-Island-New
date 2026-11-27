#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

#include "app/island.hpp"
#include "backend/timer.hpp"
#include "graphics/text.hpp"
#include "utils/struct.hpp"

namespace Api {

/// @brief Initializes the island, platform backends and render resources.
void init();

/// @brief Returns the process-wide island state.
[[nodiscard]] const Island::IslandState& get_island();

/// @brief Loads a font by family and pixel size; returns an opaque handle.
FontHandle load_font(std::string_view family, unsigned pixel_size, Text::Weight weight = Text::Weight::Regular);

/// @brief Releases a font handle.
void unload_font(FontHandle font);

/// @brief Schedules a callback after duration with the given priority.
Timer::EventHandle push_event(std::chrono::microseconds duration, void (*callback)(), float priority);

/// @brief Cancels a scheduled event; returns true if it was still pending.
bool cancel_event(Timer::EventHandle event);

/// @brief Stops the clock view and clears the scene.
void clear_status();

/// @brief Runs the frame loop until the island stops.
void run();

/// @brief Selects the EGL vendor for Wayland.
void select_for_wayland();

/// @brief Shuts down all backends and views.
void shutdown();

}
