#include "app/API.hpp"
#include "backend/wayland.hpp"
#include "graphics/renderer.hpp"
#include "scene/object.hpp"
#include "backend/timer.hpp"
#include "app/island.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"
#include "graphics/text.hpp"

using namespace std;

namespace {

const IslandConf* state{};

}

void API::init() {

    Island::init();
    state = &Island::state();
    Log::logger(Log::Debug, "Island initialized successfully");

    Wayland::init(
        static_cast<uint32_t>(state->island_width),
        static_cast<uint32_t>(state->island_height),
        state->zone,
        state->anchor_top
    );
    Log::logger(Log::Debug, "Wayland initialized successfully");

    Text::init();
    Log::logger(Log::Debug, "Text initialized successfully");

    Renderer::init();
    Log::logger(Log::Debug, "Renderer initialized successfully");

    Timer::init();
    Log::logger(Log::Debug, "Timer initialized successfully");

    Wayland::set_report_click(Object::click);

    Log::logger(Log::Debug, "Initialization completed successfully");
}

FontHandle API::load_font(string_view family, unsigned pixel_size) {
    return Text::load_font(family, pixel_size);
}

const IslandConf& API::island_state() {
    return *state;
}

// TODO: complete support for animation in the future.
void API::resize(uint32_t width, uint32_t height) {
    Wayland::request_resize(width, height);
    Island::set_size(static_cast<int>(width), static_cast<int>(height));
}

Object::Rectangle* API::add_rectangle(const RectDesc& desc) {
    return Object::add_rectangle(desc);
}

Object::Image* API::add_image(const ImageDesc& desc) {
    return Object::add_image(desc);
}

Object::Text* API::add_text(const TextDesc& desc) {
    return Object::add_text(desc);
}

void API::push_event(std::chrono::microseconds duration, void (*callback)(), float priority) {
    Timer::push(duration, callback, priority);
}

void API::clear_status() {
    Object::clear();
}

void API::run(){

    while (state->is_running) {

        if (state->need_redraw) {
            Island::request_redraw(false);
            Log::logger(Log::Debug, "Start a new frame");

            Renderer::begin_frame();
            Object::draw();
            Renderer::end_frame();
        }
        
        Island::request_redraw(Timer::wait() || state->need_redraw);
    }
}

void API::shutdown() {
    Object::clear();
    Text::shutdown();
    Renderer::shutdown();
    Wayland::shutdown();
}
