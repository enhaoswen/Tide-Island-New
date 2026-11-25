// ============================================================================
// Tide Island shared state
// ============================================================================
//
// This translation unit owns the process-wide island configuration used by the
// Wayland backend and renderer.
//
#include "app/island.hpp"
#include "backend/config.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"

#include <cmath>
#include <source_location>
#include <utility>

using namespace std;

// ============================================================================
// [Internal Details]
// ============================================================================

namespace {

Island::IslandState island{
    .island_width = 140,
    .island_height = 38,
    .zone = 40,
    .anchor_top = 2,
    .radius = 19,
    .color = Color{0, 0, 0, 1},
};

Config::IslandConfig to_config() {
    Config::IslandConfig config;
    config.island_width = island.island_width;
    config.island_height = island.island_height;
    config.zone = island.zone;
    config.anchor_top = island.anchor_top;
    config.radius = island.radius;
    config.color = island.color;
    config.blur = island.blur;
    return config;
}

} // namespace

// ============================================================================
// [Public API Implementation]
// ============================================================================

namespace Island {
    const Island::IslandState& get_island() {
        return island;
    }

    void init() {
        if (!Config::exists()) {
            Config::write(to_config());
            return;
        }

        Config::IslandConfig config = to_config();
        if (!Config::read(config))
            return;

        if (config.island_width == 0 || config.island_height == 0 ||
            !isfinite(config.radius) || config.radius < 0) {
            Log::logger(Log::Error, "Invalid island dimensions, animation scale, radius, or priority");
            return;
        }

        island.island_width = config.island_width;
        island.island_height = config.island_height;
        island.zone = config.zone;
        island.anchor_top = config.anchor_top;
        island.radius = config.radius;
        island.color = config.color;
        island.blur = config.blur;
    }

    void set_anchor_top(std::int32_t distance) {
        if (!in_range<int16_t>(distance)) {
            Log::logger(Log::Error, "Anchor distance is outside the int16 range: {}", distance);
            return;
        }
        island.anchor_top = static_cast<int16_t>(distance);
    }

    void set_is_running(bool state) {
        island.is_running = state;
    }

    void set_radius(float radius, source_location location) {
        if (!isfinite(radius) || radius < 0) {
            Log::logger(Log::Error, "{}:{}: Radius must be finite and nonnegative, value={}",
                location.file_name(), location.line(), radius);
            return;
        }

        island.radius = radius;
    }

    void set_priority(float priority) {
        if (!isfinite(priority)) {
            Log::logger(Log::Error, "Priority must be finite, value={}", priority);
            return;
        }
        island.current_priority = priority;
    }

    void set_zone(std::int32_t zone) {
        if (!in_range<int16_t>(zone)) {
            Log::logger(Log::Error, "Exclusive zone is outside the int16 range: {}", zone);
            return;
        }
        island.zone = static_cast<int16_t>(zone);
    }

    void set_size(std::uint16_t width, std::uint16_t height) {
        if (width == 0 || height == 0) {
            Log::logger(Log::Error, "Island width and height must be positive");
            return;
        }
        island.island_width = width;
        island.island_height = height;
    }

    void request_redraw(bool redraw) {
        island.need_redraw = redraw;
    }

    void set_animation_scale(float scale) {
        if (!isfinite(scale) || scale <= 0) {
            Log::logger(
                Log::Error,
                "Animation scale must be finite and positive, value={}",
                scale);
            return;
        }
        island.animation_scale = scale;
    }

    StateType get_state() {
        return island.state;
    }

    void set_state(StateType state) {
        island.state = state;
    }

} // namespace Island
