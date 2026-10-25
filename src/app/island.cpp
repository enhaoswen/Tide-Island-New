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
#include <cmath>
#include <utility>

using namespace std;
using namespace std::filesystem;

// ============================================================================
// [Internal Details]
// ============================================================================

namespace {

IslandConf island{
    .island_width  = 140,
    .island_height = 38,
    .zone          = 40,
    .anchor_top    = 2,
    .radius        = 19,
    .color         = Color{0, 0, 0, 1},
};

path get_conf_path() {
    const char* const home = getenv("HOME");
    if (home == nullptr)
        Log::fatal("HOME is not set");
    return path(home) / ".config" / "Tide Island" / "config.json";
}

} // namespace

// ============================================================================
// [Public API Implementation]
// ============================================================================

const IslandConf& Island::get_island() {
    return island;
}

void Island::init() {
    const auto conf_path = get_conf_path();

    if (conf_path.has_parent_path()) {
        create_directories(conf_path.parent_path());
    }

    const auto write_default = [&] {
        string json{};

        const auto error = glz::write<glz::opts{.prettify = true}>(
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
    const auto error  = glz::read_json(loaded, json);

    if (error) {
        Log::logger(
            Log::Error,
            "Failed to parse island configuration: {}",
            glz::format_error(error, json)
            );
        return;
    }
    if (loaded.island_width == 0 || loaded.island_height == 0 ||
        !isfinite(loaded.animation_scale) || loaded.animation_scale <= 0 ||
        !isfinite(loaded.radius) || loaded.radius < 0 || !isfinite(loaded.current_priority)) {
        Log::logger(Log::Error, "Invalid island dimensions, animation scale, radius, or priority");
        return;
    }
    island = loaded;
}

void Island::set_anchor_top(std::int32_t distance) {
    if (!in_range<int16_t>(distance)) {
        Log::logger(Log::Error, "Anchor distance is outside the int16 range: {}", distance);
        return;
    }
    island.anchor_top = static_cast<int16_t>(distance);
}

void Island::set_is_running(bool state) {
    island.is_running = state;
}

void Island::set_radius(float radius, source_location location) {
    if (!isfinite(radius) || radius < 0) {
        Log::logger(Log::Error, "{}:{}: Radius must be finite and nonnegative, value={}",
            location.file_name(), location.line(), radius);
        return;
    }

    island.radius = radius;
}

void Island::set_priority(float priority) {
    if (!isfinite(priority)) {
        Log::logger(Log::Error, "Priority must be finite, value={}", priority);
        return;
    }
    island.current_priority = priority;
}

void Island::set_zone(std::int32_t zone) {
    if (!in_range<int16_t>(zone)) {
        Log::logger(Log::Error, "Exclusive zone is outside the int16 range: {}", zone);
        return;
    }
    island.zone = static_cast<int16_t>(zone);
}

void Island::set_size(std::uint16_t width, std::uint16_t height) {
    if (width == 0 || height == 0) {
        Log::logger(Log::Error, "Island width and height must be positive");
        return;
    }
    island.island_width  = width;
    island.island_height = height;
}

void Island::request_redraw(bool redraw) {
    island.need_redraw = redraw;
}

void Island::set_animation_scale(float scale) {
    if (!isfinite(scale) || scale <= 0) {
        Log::logger(Log::Error, "Animation scale must be finite and positive, value={}", scale);
        return;
    }
    island.animation_scale = scale;
}

StateType Island::get_state() {
    return island.state;
}

void Island::set_state(StateType state) {
    island.state = state;
}
