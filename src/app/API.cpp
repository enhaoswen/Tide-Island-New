#include "app/API.hpp"
#include "backend/wayland.hpp"
#include "graphics/renderer.hpp"
#include "backend/timer.hpp"
#include "app/island.hpp"
#include "app/views/clock_view.hpp"
#include "app/state_manager.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"
#include "graphics/text.hpp"
#include "backend/egl_vendor.hpp"
#include "scene/render.hpp"

using namespace std;
using namespace std::chrono;

namespace {

const Island::IslandState* island{};

Renderer::GpuRenderer renderer{};

// Wayland input only feeds the scene's state machine and queues events; callbacks run later
// from API::run via dispatch_events(), outside Wayland dispatch.
void on_pointer_motion(float x, float y) {
    StateManager::context().pointer_move(x, y);
}

void on_pointer_button(Wayland::PointerButton button, bool pressed) {
    Object::Button b{};
    switch (button) {
    case Wayland::PointerButton::Left:
        b = Object::Button::Left;
        break;
    case Wayland::PointerButton::Right:
        b = Object::Button::Right;
        break;
    case Wayland::PointerButton::Middle:
        b = Object::Button::Middle;
        break;
    case Wayland::PointerButton::Other:
        return;
    }
    StateManager::context().pointer_button(b, pressed);
}

void on_pointer_leave() {
    StateManager::context().pointer_cancel();
}

}

void API::init() {

    Island::init();
    island = &Island::get_island();
    Log::logger(Log::Debug, "Island initialized successfully");

    Wayland::init(
        island->island_width,
        island->island_height,
        island->zone,
        island->anchor_top
    );
    Log::logger(Log::Debug, "Wayland initialized successfully");

    Text::init();
    Log::logger(Log::Debug, "Text initialized successfully");

    Renderer::init();
    Log::logger(Log::Debug, "Renderer initialized successfully");

    Timer::init();
    Log::logger(Log::Debug, "Timer initialized successfully");

    Wayland::set_pointer_callbacks({
        .motion = on_pointer_motion,
        .button = on_pointer_button,
        .leave  = on_pointer_leave,
    });

    Log::logger(Log::Debug, "Initialization completed successfully");
}

FontHandle API::load_font(string_view family, unsigned pixel_size) {
    return Text::load_font(family, pixel_size);
}

void API::unload_font(FontHandle font) {
    Text::unload_font(font);
}

const Island::IslandState& API::get_island() {
    return *island;
}

void API::resize(std::uint16_t width, std::uint16_t height) {
    Wayland::request_resize(width, height);
    Island::set_size(width, height);
}

Timer::EventHandle API::push_event(microseconds duration, void (*callback)(), float priority) {
    return Timer::push(duration, callback, priority);
}

bool API::cancel_event(Timer::EventHandle event) {
    return Timer::cancel(event);
}

void API::clear_status() {
    ClockView::stop();
    StateManager::context().clear();
}

void API::run() {
    auto  last = steady_clock::now();
    auto& ctx  = StateManager::context();

    while (island->is_running) {

        if (island->need_redraw) {
            Island::request_redraw(false);

            const auto  now   = steady_clock::now();
            const float dt_ms = static_cast<float>(duration_cast<microseconds>(now - last).count()) / 1000.0f;
            last              = now;

            StateManager::update(); // center the root on the surface
            ctx.update(dt_ms);      // step animations, then layout

            // Animation / layout can move nodes under a still pointer (hover changes). Lay out
            // again if callbacks ran so their changes make it into this frame.
            if (ctx.dispatch_events())
                ctx.layout();

            Renderer::begin_frame();
            Object::draw(ctx, renderer);
            Renderer::end_frame();
        }

        const bool redraw = Timer::wait(ctx.animating());
        // Pointer input gathered during the wait; callbacks usually change something visible.
        const bool handled = ctx.dispatch_events();
        Island::request_redraw(redraw || handled || island->need_redraw);
    }
}

void API::shutdown() {
    ClockView::shutdown();
    StateManager::stop();
    Timer::shutdown();
    Text::shutdown();
    Renderer::shutdown();
    Wayland::shutdown();
}

void API::select_for_wayland() {
    EglVendor::select_for_wayland();
}
