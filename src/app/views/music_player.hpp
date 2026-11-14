#pragma once

#include "scene/context.hpp"
#include "utils/struct.hpp"

namespace MusicPlayerView {

struct Fonts {
    FontHandle title{};
    FontHandle artist{};
    FontHandle time{};
    FontHandle icon{};
};

void start(Object::Handle bg, const Fonts& fonts);

void stop();

}