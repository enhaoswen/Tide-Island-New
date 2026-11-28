#pragma once

#include <cstdint>
#include <source_location>

#include "utils/color.hpp"
#include "utils/struct.hpp"

namespace Island {

/// @brief Process-wide island configuration and runtime state.
struct IslandState {
	uint16_t  island_width_{};
	uint16_t  island_height_{};
	int16_t   zone_{-1};
	int16_t   anchor_top_{};
	float     animation_scale_{1.f};
	float     radius_{};
	float     current_priority_{};
	Color     color_{};
	bool      should_redraw_{true};
	bool      is_running_{true};
	StateType state_{StateType::Clock};
};

inline constexpr float k_priority_normal{0.f};
inline constexpr float k_priority_hover{1.f};
inline constexpr float k_priority_transition{2.f};
inline constexpr float k_priority_sys_info{3.f};
inline constexpr float k_priority_expended{5.f};

/// @brief Returns the process-wide island state.
[[nodiscard]] const IslandState& get_island();

/// @brief Loads the island configuration from disk, or writes the defaults.
void init();

/// @brief Sets the island's distance from the top edge in pixels.
void set_anchor_top(int32_t distance);

/// @brief Starts or stops the frame loop.
void set_is_running(bool is_running);

/// @brief Sets the island corner radius in pixels.
void set_radius(float radius, std::source_location location = std::source_location::current());

/// @brief Sets the exclusive zone in pixels.
void set_zone(int32_t zone);

/// @brief Requests whether the next frame should be drawn.
void request_redraw(bool should_redraw);

/// @brief Sets the current animation priority.
void set_priority(float priority);

/// @brief Sets the island size in pixels.
void set_size(uint16_t width, uint16_t height);

/// @brief Sets the multiplier applied to animation durations.
void set_animation_scale(float scale);

/// @brief Returns the active island state.
[[nodiscard]] StateType get_state();

/// @brief Switches the active island state.
void set_state(StateType state);

}
