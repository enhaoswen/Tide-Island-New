#include "state_manager.hpp"
#include "API.hpp"
#include "island.hpp"
#include "views/clock_view.hpp"
#include "backend/wayland.hpp"

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

void resize_root(float value, AnimationTarget target, uint32_t duration = 300) {

    resources.root->add_animation({
        .to       = value,
        .priority = 1,
        .duration = duration,
        .target   = target,
    });
}

}

void StateManager::init(Resources& arg_resources) {
    resources = arg_resources;
    update();
}

void StateManager::update() {
    if (auto* root = resources.root.get()) {
        const auto size = Wayland::get_surface_size();
        auto frame = root->get_frame();
        frame.x = (static_cast<float>(size[0]) - frame.width) / 2.0f;
        frame.y = 0;
        root->set_frame(frame);
    }
}

void StateManager::switch_state(StateType state) {
    stop();

    Island::set_state(state);

    if (state == StateType::Clock) {
        API::resize(140,38);
        resize_root(140, AnimationTarget::Width);
        resize_root(38, AnimationTarget::Height);

        ClockView::start(resources.root, resources.main_font);
    }

    else if (state == StateType::MusicPLayer) {
        API::resize(410,165);
        resize_root(410, AnimationTarget::Width);
        resize_root(165, AnimationTarget::Height);
    }
}

void StateManager::stop() {

    switch (Island::get_state()) {

    case StateType::Clock:
        ClockView::stop();
        break;

    default:
        break;
    }
}
