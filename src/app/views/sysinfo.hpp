#pragma once

#include <cstdint>

#include "scene/context.hpp"
#include "scene/struct.hpp"
#include "utils/struct.hpp"

// Transient OSD for volume / brightness / power changes. It owns the SysInfo listener for
// the whole session and switches the island into StateType::Osd on its own.
namespace SysInfoView {

/// @brief Font handles used by the OSD.
struct Fonts {
	FontHandle value_{};
	FontHandle label_{};
};

/// @brief Registers the OSD context, fonts and state-switch callback.
/// `switch_state` is how the OSD opens itself and later returns to the previous state.
void init(Object::Context& ctx, const Fonts& fonts, void (*switch_state)(StateType));

/// @brief Removes the SysInfo listener and stops the OSD.
void shutdown();

/// @brief Builds and shows the OSD on the given background.
void start(Object::Handle bg);

/// @brief Hides the OSD and destroys its nodes.
void stop();

/// @brief Returns the OSD width in pixels for the current kind.
[[nodiscard]] uint32_t width();

}
