#include "app/island.hpp"

#include <algorithm>
#include <cmath>
#include <source_location>
#include <utility>

#include "backend/config.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"

using namespace std;

namespace {

Island::IslandState island{};

}

namespace Island {
const Island::IslandState& get_island() {
	return island;
}

void init() {
	const Config::TideConfig& config{Config::get_config()};
	if(config.island_width == 0 || config.island_height == 0 || !isfinite(config.radius) || config.radius < 0) {
		Log::fatal("Invalid island dimensions, animation scale, radius, or priority");
	}

	island.island_width_  = config.island_width;
	island.island_height_ = config.island_height;
	island.zone_          = config.zone;
	island.anchor_top_    = config.anchor_top;
	island.radius_        = config.radius;
	island.color_         = config.color;
}

void set_anchor_top(int32_t distance) {
	if(!in_range<int16_t>(distance)) {
		Log::logger(Log::LogLevel::Error, "Anchor distance is outside the int16 range: {}", distance);
		return;
	}
	island.anchor_top_ = static_cast<int16_t>(distance);
}

void set_is_running(bool is_running) {
	island.is_running_ = is_running;
}

void set_radius(float radius, source_location location) {
	if(!isfinite(radius) || radius < 0) {
		Log::logger(Log::LogLevel::Error,
			"{}:{}: Radius must be finite and nonnegative, value={}",
			location.file_name(),
			location.line(),
			radius);
		return;
	}

	island.radius_ = radius;
}

void set_priority(float priority) {
	if(!isfinite(priority)) {
		Log::logger(Log::LogLevel::Error, "Priority must be finite, value={}", priority);
		return;
	}
	island.current_priority_ = priority;
}

void set_zone(int32_t zone) {
	if(!in_range<int16_t>(zone)) {
		Log::logger(Log::LogLevel::Error, "Exclusive zone is outside the int16 range: {}", zone);
		return;
	}
	island.zone_ = static_cast<int16_t>(zone);
}

void set_size(uint16_t width, uint16_t height) {
	if(width == 0 || height == 0) {
		Log::logger(Log::LogLevel::Error, "Island width and height must be positive");
		return;
	}
	island.island_width_  = width;
	island.island_height_ = height;
}

void request_redraw(bool should_redraw) {
	island.should_redraw_ = should_redraw;
}

void set_animation_scale(float scale) {
	if(!isfinite(scale) || scale <= 0) {
		Log::logger(Log::LogLevel::Error, "Animation scale must be finite and positive, value={}", scale);
		return;
	}
	island.animation_scale_ = scale;
}

StateType get_state() {
	return island.state_;
}

void set_state(StateType state) {
	island.state_ = state;
}

}
