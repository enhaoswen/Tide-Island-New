// ============================================================================
// Tide Island shared state
// ============================================================================
//
// This translation unit owns the process-wide island configuration used by the
// Wayland backend and renderer.
//
#include "app/island.hpp"
#include "utils/struct.hpp"
#include "utils/log.hpp"
#include "utils/config.hpp"
#include <source_location>

using namespace std;

// ============================================================================
// [Internal Details]
// ============================================================================

namespace {

IslandConf island{};

} // namespace

// ============================================================================
// [Public API Implementation]
// ============================================================================

const IslandConf& Island::state() {
    return island;
}

void Island::init(Config& config){
    island = config.to_struct();
}

void Island::set_anchor_top(int distance) {
    island.anchor_top = distance;
}

void Island::set_is_running(bool state) {
    island.is_running = state;
}

void Island::set_radius(float radius, source_location location) {
    if (radius <= 0) {
        Log::logger(Log::Error, "{}:{}: Radius has to be positive, value={}", location.file_name(), location.line(), radius);
        radius = 0;
    }

    island.radius = radius;
}

void Island::set_zone(int zone) {
    island.zone = zone;
}

void Island::set_island_width(int width){
    island.island_width = width;
}

void Island::set_island_height(int height){
    island.island_height = height;
}

void Island::request_redraw(bool redraw) {
    island.need_redraw = redraw;
}
