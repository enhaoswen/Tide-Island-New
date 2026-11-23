#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

#include "app/island.hpp"
#include "backend/timer.hpp"
#include "graphics/text.hpp"
#include "utils/struct.hpp"

namespace API {

void init();
const Island::IslandState& get_island();
FontHandle load_font(std::string_view family, unsigned pixel_size,
                     Text::Weight weight = Text::Weight::Regular);
void unload_font(FontHandle font);
Timer::EventHandle push_event(std::chrono::microseconds duration, void (*callback)(), float priority);
bool cancel_event(Timer::EventHandle event);
void clear_status();
void run();
void select_for_wayland();

void shutdown();
} // namespace API
