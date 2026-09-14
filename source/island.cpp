// ============================================================================
// Tide Island shared state
// ============================================================================
//
// This translation unit owns the process-wide island configuration used by the
// Wayland backend and renderer.
//
#include "island.hpp"
#include "struct.hpp"
#include "log.hpp"
#include "config.hpp"
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
    island = get<IslandConf>(config.to_struct());
}

void Island::set_anchor_top(float distance) {
    island.anchor_top = distance;
}

void Island::set_is_running(bool state) {
    island.is_running = state;
}

void Island::set_radius(float radius, source_location location) {
    if (radius <= 0) {
        Log::logger(Log::Error, R"(Radius has to be positive "{}":{})",location.file_name(), location.line());
    }

    island.radius = radius;
}

void Island::set_zone(int zone) {
    island.zone = zone;
}

void Island::set_island_width(float width){
    island.island_width = width;
}

void Island::set_island_height(float height){
    island.island_height = height;
}

void Island::request_redraw(bool redraw) {
    island.need_redraw = redraw;
}