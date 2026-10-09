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

void API::resize(uint32_t width, uint32_t height) {
    Wayland::request_resize(width, height);
}

void API::draw_rectangle(RectDesc& rect_desc){
    Object::add_rectangle(rect_desc);
}

void API::draw_image(ImageDesc& desc) {
    Object::add_image(desc);
}

void API::draw_text(TextDesc &desc){
    Object::add_text(desc);
}

void API::run(){

    while (state->is_running) {

        if (state->need_redraw) {
            Log::logger(Log::Debug, "Start a new frame");

            Renderer::begin_frame();
            Object::draw();
            Renderer::end_frame();
        }
        
       Island::request_redraw(Timer::wait());
    }

}

void API::shutdown() {

}
