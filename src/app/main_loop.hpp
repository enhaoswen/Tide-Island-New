#pragma once

namespace MainLoop {

/// @brief Polls all event sources; returns true when a redraw is needed.
[[nodiscard]] bool wait(bool is_animating);

}
