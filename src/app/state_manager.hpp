#pragma once

#include "scene/context.hpp"
#include "utils/struct.hpp"

namespace StateManager {

struct Resources {
    FontHandle main_font{};
};

void init(const Resources& resources);

// Center the root on the surface; run every frame before layout so resize
// animations stay centered.
void update();

void switch_state(StateType state);

// Stop the active view and destroy the root.
void stop();

[[nodiscard]] Object::Context& context();

} // namespace StateManager
