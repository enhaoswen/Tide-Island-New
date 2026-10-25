#pragma once

#include "utils/struct.hpp"

#include <source_location>

// ============================================================================
// Tide Island shared state API
// ============================================================================
//
// The island state is intentionally small and process-wide. Platform and render
// backends read this state while public setters validate updates.

namespace Island {

const IslandConf& get_island();

void init();

void set_anchor_top(std::int32_t distance);

void set_is_running(bool state);

void set_radius(float radius, std::source_location location = std::source_location::current());

void set_zone(std::int32_t zone);

void request_redraw(bool redraw);

void set_priority(float priority);

void set_size(std::uint16_t width, std::uint16_t height);

void set_animation_scale(float scale);

StateType get_state();
void set_state(StateType state);

} // namespace Island
