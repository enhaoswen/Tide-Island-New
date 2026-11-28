#pragma once

#include <chrono>

namespace MainLoop {

/// @brief Polls all event sources; returns true when a redraw is needed.
/// @param frame_delay Time until the frame cap allows the next frame; zero or less when uncapped or already due.
[[nodiscard]] bool wait(bool is_animating, std::chrono::nanoseconds frame_delay);

}
