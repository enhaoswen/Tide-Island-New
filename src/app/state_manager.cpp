#include "state_manager.hpp"

#include <cmath>
#include <string_view>

#include "island.hpp"
#include "backend/wayland.hpp"
#include "scene/animation.hpp"
#include "utils/log.hpp"
#include "views/clock_view.hpp"
#include "views/music_player.hpp"

using namespace std;

// Tide Island QS size

// Size of Clock: 140*38                                      radius 19
// Size of Lyrics: dynamic*38 (minimum width: 220)            radius 19
// Size of Custom: dynamic*38 (minimum width: 220)            radius 19
// Size of Split: 140*38 / 220*38 / 248*38 (icon/text/progress)radius 19
// Size of Workspace (long_capsule): 220*38                   radius 19
// Size of MusicPlayer (expanded): 410*165                    radius 32
// Size of BluetoothExpanded: 410*165                         radius 32
// Size of ControlCenter: 420*352                             radius 28
// Size of ControlCenterPowerView: 420*150                    radius 28
// Size of NotificationCenter: 410*dynamic                    radius 28
// Size of WallpaperPicker: 1100*260                          radius 28
// Size of ApplicationLauncher: 1100*260                      radius 28
// Size of FileShelf: 1100*260                                radius 28
// Size of Clipboard: 520*350                                 radius 28
// Size of ClipboardImagePreview: 520*400                     radius 28
// Size of Weather: 460*340                                   radius 28
// Size of Calendar: 420*340                                  radius 28
// Size of Notification: (272–400)*(56 or 68)                 radius 28 (56/2) 或 34 (68/2)
// Size of NotificationExpanded: 520*(84–240)                 radius 28

namespace {

struct StateSize {
    uint32_t w, h;
    float    r;
};

constexpr StateSize size_of(StateType t) {
    switch (t) {
    case StateType::Clock:
        return {140, 38, 19};
    case StateType::Lyrics:
        return {220, 38, 19};
    case StateType::MusicPLayer:
        return {410, 165, 32};
    case StateType::ControlCenter:
        return {420, 352, 28};
    }
    return {140, 38, 19};
}

Island::IslandState     island_state;
StateManager::Resources resources;
Object::Context         ctx{};
Object::Handle          root{};
Object::Handle          bg{};
int                     trace_left{0};
// Root size to switch to once the Wayland surface has actually been resized.
float                   root_width{};
float                   root_height{};

constexpr float k_hover_scale = 1.03f;

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
            is_expended(Island::get_state()) ? StateType::Clock : StateType::MusicPLayer);
    }
}

void set_surface(const uint32_t width, const uint32_t height) {
    Wayland::request_resize(width, height);
    root_width  = static_cast<float>(width);
    root_height = static_cast<float>(height);
}

void resize(const uint32_t width, const uint32_t height, bool expended, float radius = -1) {

    AnimCallback on_done{};
    if (!expended) {
        on_done = [width, height] {
            if (!ctx.is_animating(bg, Prop::W) && !ctx.is_animating(bg, Prop::H)) {
                StateManager::trace_frame("on_done(before resize)");
                trace_left = 6;
                set_surface(width, height);
                Island::request_redraw(true);
                StateManager::trace_frame("on_done(after resize)");
            }
        };
    }

    ctx.animate(Object::SAnimDesc{
        .h       = bg,
        .to      = static_cast<float>(width),
        .s       = Springs::Smooth,
        .delay   = 0,
        .p       = Prop::W,
        .on_done = on_done,
    });

    ctx.animate(Object::SAnimDesc{
        .h       = bg,
        .to      = static_cast<float>(height),
        .s       = Springs::Smooth,
        .delay   = 0,
        .p       = Prop::H,
        .on_done = on_done,
    });

    if (radius != -1.f) {
        ctx.animate(Object::TAnimDesc{
            .h       = bg,
            .to      = radius,
            .t       = Timings::Smooth,
            .delay   = 0,
            .p       = Prop::Radius,
            .on_done = nullptr,
        });
    }

    if (expended)
        set_surface(width, height);
}

void scale_bg(const float to, AnimCallback on_done = {}) {
    ctx.animate(Object::SAnimDesc{
        .h       = bg,
        .to      = to,
        .s       = Springs::Smooth,
        .delay   = 0,
        .p       = Prop::Scale,
        .on_done = std::move(on_done),
    });
}

void on_hovered(const Object::Event&) {
    const StateType state = Island::get_state();
    if (is_expended(state))
        return;

    const StateSize size = size_of(state);

    if (ctx.hovered() == bg) {
        const auto grown = [](const uint32_t v) {
            return v + static_cast<uint32_t>(ceil(static_cast<float>(v) * (k_hover_scale - 1.f)));
        };
        set_surface(grown(size.w), grown(size.h));
        scale_bg(k_hover_scale);
    }
    else {
        scale_bg(1.f, [size] {
            set_surface(size.w, size.h);
            Island::request_redraw(true);
        });
    }
}

}

void StateManager::init(const Resources& arg_resources) {
    island_state = Island::get_island();
    resources    = arg_resources;

    root        = ctx.create_root(island_state.island_width, island_state.island_height);
    root_width  = static_cast<float>(island_state.island_width);
    root_height = static_cast<float>(island_state.island_height);

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
    ctx.set_pivot(bg, .5f, 0.f);
    ctx.on(bg, Object::EventType::HoverEnter | Object::EventType::HoverLeave, on_hovered);
}

Object::Context& StateManager::context() {
    return ctx;
}

void StateManager::sync_surface() {
    // Root follows the surface: it may only change in the frame the new buffer is used.
    Wayland::sync_surface();
    if (!root)
        return;
    if (!Wayland::resize_pending()) {
        ctx.set_size(root, root_width, root_height);
        ctx.layout();
    }

    Wayland::set_blur(island_state.blur);
    if (island_state.blur) {
        const Frame f = ctx.absolute_frame(bg);
        Wayland::set_blur_shape(f.x, f.y, f.width, f.height, ctx.rect(bg).radius());
    }
}

void StateManager::trace_frame(const char* tag) {
    if (!root || (trace_left <= 0 && !string_view{tag}.contains("on_done")))
        return;
    const auto b  = ctx.frame(bg);
    const auto r  = ctx.frame(root);
    const auto sz = Wayland::get_surface_size();
    const auto bf = Wayland::get_buffer_size();
    Log::logger(Log::Debug,
        "[trace] {} bg=({:.2f},{:.2f},{:.2f}x{:.2f}) root={:.2f}x{:.2f} surface={}x{} buffer={}x{} scale={:.3f} animW={} animH={}",
        tag, b.x, b.y, b.width, b.height, r.width, r.height, sz[0], sz[1], bf[0], bf[1],
        Wayland::get_scale(), ctx.is_animating(bg, Prop::W), ctx.is_animating(bg, Prop::H));
    if (trace_left > 0 && !string_view{tag}.contains("on_done"))
        --trace_left;
}

void StateManager::switch_state(const StateType state) {
    stop();

    Island::set_state(state);
    if (ctx.scale(bg) != 1.f || ctx.is_animating(bg, Prop::Scale))
        scale_bg(1.f);

    if (state == StateType::Clock) {
        resize(140, 38, false, 19);
        ClockView::start(bg, resources.main_font);
    }

    else if (state == StateType::MusicPLayer) {
        resize(410, 165, true, 32);
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