#pragma once

#include "scene/context.hpp"
#include "utils/struct.hpp"

namespace StateManager {

struct Resources {
    FontHandle main_font{};
    FontHandle title{};
    FontHandle artist{};
    FontHandle time{};
    FontHandle icon{};
};

void init(const Resources& resources);

void update();

void switch_state(StateType state);

// Stop the active view and destroy the root.
void stop();

[[nodiscard]] Object::Context& context();

}