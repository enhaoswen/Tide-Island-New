#pragma once

#include "scene/context.hpp"
#include "scene/struct.hpp"
#include "utils/struct.hpp"

#include <cstdint>

// Transient OSD for volume / brightness / power changes. It owns the SysInfo listener for
// the whole session and switches the island into StateType::Osd on its own.
namespace SysInfoView {

struct Fonts {
    FontHandle value{};
    FontHandle label{};
};

// `switch_state` is how the OSD opens itself and later returns to the previous state.
void init(Object::Context& ctx, const Fonts& fonts, void (*switch_state)(StateType));
void shutdown();

void start(Object::Handle bg);
void stop();

[[nodiscard]] std::uint32_t width();

} // namespace SysInfoView
