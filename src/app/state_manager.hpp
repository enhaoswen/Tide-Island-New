#pragma once

#include "scene/context.hpp"
#include "utils/struct.hpp"

namespace StateManager {

/// @brief Font handles used by the island views.
struct Resources {
	FontHandle main_font_{};
	FontHandle title_{};
	FontHandle artist_{};
	FontHandle time_{};
	FontHandle icon_{};
};

/// @brief Initializes the scene, background and views.
void init(const Resources& resources);

/// @brief Applies a staged surface resize and resizes the root to match.
/// Call every frame after animations/events and right before drawing.
void sync_surface();

/// @brief Stops the active view and switches to the given state.
void switch_state(StateType state);

/// @brief Stops the active view and destroys the root.
void stop();

/// @brief Returns the shared scene context.
[[nodiscard]] Object::Context& get_context();

}
