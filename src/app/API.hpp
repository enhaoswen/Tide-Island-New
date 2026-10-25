#pragma once

#include <cstdint>

#include "utils/struct.hpp"
#include "scene/object.hpp"
#include "backend/event_handle.hpp"

namespace API {

void init();
const IslandConf& get_island();
void resize(std::uint16_t width, std::uint16_t height);
FontHandle load_font(std::string_view family, unsigned pixel_size);
void unload_font(FontHandle font);
Timer::EventHandle push_event(std::chrono::microseconds duration, void (*callback)(), float priority);
bool cancel_event(Timer::EventHandle event);
void clear_status();
void run();
void select_for_wayland();

void shutdown();
}
