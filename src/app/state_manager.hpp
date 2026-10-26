#pragma once

#include "utils/struct.hpp"

// we will only include this header in main, so we can put what every header here.
// no need to consider about header pollution
#include "scene/object.hpp"

namespace StateManager {

struct Resources {
    FontHandle main_font{};
    Object::Handle<Object::Layout> root{};
};

void init(Resources& resources);
// Center the root after animations, before laying out its children.
void update();
void switch_state(StateType state);
void stop();

}
