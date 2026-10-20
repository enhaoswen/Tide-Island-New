#pragma once

#include "utils/struct.hpp"

namespace AnimationManager {

Animation* add_animation(const AnimationDesc& anim_desc);
void remove_animation(Animation* anim);
bool has_active();
void update();
void clear();

}
