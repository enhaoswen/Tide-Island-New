#include "state_manager.hpp"

#include "API.hpp"
#include "island.hpp"
#include "backend/wayland.hpp"
#include "scene/animation.hpp"
#include "views/clock_view.hpp"
#include "views/music_player.hpp"

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

Island::IslandState     island_state;
StateManager::Resources resources;
Object::Context         ctx{};
Object::Handle          root{};
Object::Handle          bg{};

bool is_expended(StateType type) {
    switch (type) {
    case StateType::Clock:
    case StateType::Lyrics:
        return false;

    case StateType::MusicPLayer:
    case StateType::ControlCenter:
        return true;
    }

    return false;
}

void on_bg_click(const Object::Event& e) {
    if (e.button == Object::Button::Left) {
        StateManager::switch_state(
            is_expended(Island::get_state()) ? StateType::Clock : StateType::MusicPLayer
            );
    }
}

void resize(const uint32_t width, const uint32_t height, bool expended) {
    ctx.set_size(root, static_cast<float>(width), static_cast<float>(height));

    AnimCallback on_done{};
    if (!expended) {
        on_done = [width, height] {
            if (!ctx.is_animating(bg, Prop::W) && !ctx.is_animating(bg, Prop::H)) {
                Wayland::request_resize(width, height);
                Island::request_redraw(true);
            }
        };
    }

    ctx.animate(Object::SAnimDesc{
        .h       = bg,
        .to      = static_cast<float>(width),
        .s       = Springs::Smooth,
        .delay   = 0,
        .p       = Prop::W,
        .on_done = nullptr,
    });

    ctx.animate(Object::SAnimDesc{
        .h       = bg,
        .to      = static_cast<float>(height),
        .s       = Springs::Smooth,
        .delay   = 0,
        .p       = Prop::H,
        .on_done = on_done,
    });

    if (expended) {
        Wayland::request_resize(width, height);
    }
}

}

void StateManager::init(const Resources& arg_resources) {
    island_state = Island::get_island();
    resources    = arg_resources;

    root = ctx.create_root(island_state.island_width, island_state.island_height);

    bg = ctx.create_rect(root, Object::RectDesc{
        .frame = Frame{
            .x      = 0.f,
            .y      = 0.f,
            .width  = static_cast<float>(island_state.island_width),
            .height = static_cast<float>(island_state.island_height),
        },

        .color  = island_state.color,
        .radius = island_state.radius,
    });

    ctx.set_layout(root, Object::LayoutData{
        .dir     = Object::Direction::Row,
        .justify = Object::Justify::Center,
    });

    ctx.set_size_mode(bg, Object::Axis::Horizontal, Object::SizeMode::Fixed);
    ctx.set_size_mode(bg, Object::Axis::Vertical, Object::SizeMode::Fixed);
    ctx.on_click(bg, on_bg_click);
}

Object::Context& StateManager::context() {
    return ctx;
}

void StateManager::update() {
    if (!root)
        return;

}

void StateManager::switch_state(const StateType state) {
    stop();

    Island::set_state(state);

    if (state == StateType::Clock) {
        resize(140, 38, false);
        ClockView::start(bg, resources.main_font);
    }

    else if (state == StateType::MusicPLayer) {
        resize(410, 165, true);
        MusicPlayerView::start(
            bg,
            {
                .title  = resources.title,
                .artist = resources.artist,
                .time   = resources.time,
                .icon   = resources.icon,
            });
    }

    Island::request_redraw(true);
}

void StateManager::stop() {
    switch (Island::get_state()) {
    case StateType::Clock:
        ClockView::stop();
        break;
    case StateType::MusicPLayer:
        MusicPlayerView::stop();
        break;
    default:
        break;
    }
}