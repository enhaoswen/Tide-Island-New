#pragma once

#include "scene/context.hpp"
#include "scene/struct.hpp"
#include "utils/struct.hpp"

namespace MusicPlayerView {

/// @brief Font handles used by the music player view.
struct Fonts {
	FontHandle title_{};
	FontHandle artist_{};
	FontHandle time_{};
	FontHandle icon_{};
};

/// @brief Builds and starts the music player view on the given background.
void start(Object::Context& ctx, Object::Handle bg, const Fonts& fonts);

/// @brief Stops the music player and destroys its nodes.
void stop();

}
