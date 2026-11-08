#include "state_manager.hpp"

#include "API.hpp"
#include "island.hpp"
#include "backend/wayland.hpp"
#include "views/clock_view.hpp"

// Tide Island QS size

// Size of Clock: 140*38
// Size of Lyrics: dynamic*38 (minimum width: 220)
// Size of Custom: dynamic*38 (minimum width: 220)
// Size of Split: 140*38 / 220*38 / 248*38 (icon / text / progress)
// Size of Workspace (long_capsule): 220*38
// Size of MusicPlayer (expanded): 410*165
// Size of BluetoothExpanded: 410*165
// Size of ControlCenter: 420*352
// Size of ControlCenterPowerView: 420*150
// Size of NotificationCenter: 410*dynamic
// Size of WallpaperPicker: 1100*260
// Size of ApplicationLauncher: 1100*260
// Size of FileShelf: 1100*260
// Size of Clipboard: 520*350
// Size of ClipboardImagePreview: 520*400
// Size of Weather: 460*340
// Size of Calendar: 420*340
// Size of Notification: (272–400)*(56 or 68)
// Size of NotificationExpanded: 520*(84–240)

namespace {

StateManager::Resources resources;
Object::Context         ctx{};
Object::Handle          root{};

}

void StateManager::init(const Resources& arg_resources) {
    resources = arg_resources;
}

Object::Context& StateManager::context() {
    return ctx;
}

void StateManager::update() {
    if (!root)
        return;

    const auto  size  = Wayland::get_surface_size();
    const Frame frame = ctx.frame(root);
    ctx.set_pos(root, (static_cast<float>(size[0]) - frame.width) * 0.5f, 0.0f);
}

void StateManager::switch_state(StateType state) {
    stop();

    Island::set_state(state);

    if (state == StateType::Clock) {
        API::resize(140, 38);
        root = ctx.create_root(140.0f, 38.0f);
        ClockView::start(root, resources.main_font);
    }
    else if (state == StateType::MusicPLayer) {
        API::resize(410, 165);
        root = ctx.create_root(410.0f, 165.0f);
    }

    Island::request_redraw(true);
}

void StateManager::stop() {
    switch (Island::get_state()) {
    case StateType::Clock:
        ClockView::stop();
        break;
    default:
        break;
    }

    if (root) {
        ctx.destroy(root);
        root = {};
    }
}
