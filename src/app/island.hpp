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

const IslandConf& state();
void init();
void set_anchor_top(int distance);
void set_is_running(bool state);
void set_radius(float radius, std::source_location location = std::source_location::current());
void set_zone(int zone);
void request_redraw(bool redraw);
void set_priority(float priority);
void set_size(int width, int height);

} // namespace Island
