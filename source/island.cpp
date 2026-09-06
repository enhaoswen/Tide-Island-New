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

using namespace std;
using namespace nlohmann;

// ============================================================================
// [Internal Details]
// ============================================================================

namespace {

Island_conf island{};

} // namespace

// ============================================================================
// [Public API Implementation]
// ============================================================================

const Island_conf* Island::state() {
    return &island;
}

Island_conf init_island(){
    json config = Config::get_config();

    Island_conf island {
        .color = {0,0,0,1},
        .island_width = config["island_width"],
        .island_height = config["island_height"],
        .zone = config["zone"],
        .anchor_top = config["anchor_top"],
        .radius = config["radius"],
        .is_running = true,
    };

    return island;
}

void Island::init(){
    Config::init();
    island = init_island();
}

void Island::set_anchor_top(float distance) {
    island.anchor_top = distance;
}

void Island::set_is_running(bool state) {
    island.is_running = state;
}

void Island::set_radius(float radius) {
    if (radius <= 0) {
        Log::fatal("Radius has to be positive");
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