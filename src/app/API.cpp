#include "app/API.hpp"
#include "backend/wayland.hpp"
#include "graphics/renderer.hpp"
#include "scene/object.hpp"
#include "backend/timer.hpp"
#include "app/island.hpp"
#include "app/views/clock_view.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"
#include "graphics/text.hpp"
#include "backend/egl_vendor.hpp"

using namespace std;

namespace {

const IslandConf* island{};

}

void API::init() {

    Island::init();
    island = &Island::get_island();
    Log::logger(Log::Debug, "Island initialized successfully");

    Wayland::init(
        static_cast<uint32_t>(island->island_width),
        static_cast<uint32_t>(island->island_height),
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

    Wayland::set_report_click(Object::click);

    Log::logger(Log::Debug, "Initialization completed successfully");
}

FontHandle API::load_font(string_view family, unsigned pixel_size) {
    return Text::load_font(family, pixel_size);
}

void API::unload_font(FontHandle font) {
    Text::unload_font(font);
}

const IslandConf& API::get_island() {
    return *island;
}

// TODO: complete support for animation in the future.
void API::resize(uint32_t width, uint32_t height) {
    Wayland::request_resize(width, height);
    Island::set_size(static_cast<int>(width), static_cast<int>(height));
}

Timer::EventHandle API::push_event(std::chrono::microseconds duration, void (*callback)(), float priority) {
    return Timer::push(duration, callback, priority);
}

bool API::cancel_event(Timer::EventHandle event) {
    return Timer::cancel(event);
}

void API::clear_status() {
    ClockView::stop();
    Object::clear();
}

void API::run(){

    while (island->is_running) {

        if (island->need_redraw) {
            Island::request_redraw(false);
            Object::update();
            Renderer::begin_frame();
            Object::draw();
            Renderer::end_frame();
        }
        
        Island::request_redraw(Timer::wait() || island->need_redraw);
    }
}

void API::shutdown() {
    ClockView::shutdown();
    Timer::shutdown();
    Object::clear();
    Text::shutdown();
    Renderer::shutdown();
    Wayland::shutdown();
}

void API::select_for_wayland() {
    EglVendor::select_for_wayland();
}