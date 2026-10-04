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
#include "utils/color_json.hpp"

#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
#include <source_location>
#include <fstream>
#include <filesystem>

using namespace std;
using namespace std::filesystem;

// ============================================================================
// [Internal Details]
// ============================================================================

namespace {


IslandConf island{
    .island_width = 140,
    .island_height = 38,
    .zone = 40,
    .anchor_top = 2,
    .radius = 0,
    .color = Color{0, 0, 0, 0},
};

path get_conf_path() {
    const char* home = getenv("HOME");
    if (home == nullptr) Log::fatal("HOME is not set");
    return path(home) / ".config" / "Tide Island" / "config.json";
}

} // namespace

// ============================================================================
// [Public API Implementation]
// ============================================================================

const IslandConf& Island::state() {
    return island;
}

void Island::init() {
    const path& conf_path = get_conf_path();

    if (conf_path.has_parent_path()) {
        create_directories(conf_path.parent_path());
    }

    auto write_default = [&]() {
        string json;

        auto error = glz::write<glz::opts{.prettify = true}>(
            island,
            json
        );

        if (error) {
            Log::logger(
                Log::Error,
                "Failed to serialize island configuration: {}",
                glz::format_error(error, json)
            );
            return false;
        }

        ofstream out(conf_path, ios::trunc);

        if (!out) {
            Log::logger(
                Log::Error,
                "Failed to open configuration file for writing: {}",
                conf_path.string()
            );
            return false;
        }

        out << json;
        return true;
    };

    if (!exists(conf_path)) {
        write_default();
        return;
    }

    ifstream in(conf_path, ios::binary);

    if (!in) {
        Log::logger(
            Log::Error,
            "Failed to open configuration file: {}",
            conf_path.string()
        );
        return;
    }

    string json{
        istreambuf_iterator<char>{in},
        istreambuf_iterator<char>{}
    };

    if (json.empty()) {
        write_default();
        return;
    }

    auto loaded = island;
    auto error = glz::read_json(loaded, json);

    if (error) {
        Log::logger(
            Log::Error,
            "Failed to parse island configuration: {}",
            glz::format_error(error, json)
        );
        return;
    }
    island = loaded;
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
