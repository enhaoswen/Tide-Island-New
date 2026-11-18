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

// Apply a staged surface resize (if the EGL driver is ready) and resize the root to match.
// Call every frame after animations/events and right before drawing.
void sync_surface();

// DEBUG: logs geometry for a few frames after a shrink finishes.

void trace_frame(const char* tag);

void switch_state(StateType state);

// Stop the active view and destroy the root.
void stop();

[[nodiscard]] Object::Context& context();

}