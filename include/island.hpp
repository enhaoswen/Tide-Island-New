#pragma once

#include "struct.hpp"

// ============================================================================
// Tide Island shared state API
// ============================================================================
//
// The island state is intentionally small and process-wide. Platform and render
// backends read this state while public setters validate updates.
//
namespace Island {

const Island_conf* state();
void init();
void set_anchor_top(float distance);
void set_is_running(bool state);
void set_radius(float radius);
void set_zone(int zone);
void request_redraw(bool redraw);
void set_island_width(float width);
void set_island_height(float height);
} // namespace Island
