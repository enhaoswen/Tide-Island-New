#include "API.hpp"
#include "wayland.hpp"
#include "renderer.hpp"
#include "object.hpp"
#include "timer.hpp"
#include "config.hpp"
#include "island.hpp"
#include "log.hpp"
#include "struct.hpp"

namespace {

const IslandConf* state{};

}

void API::init() {

    Config island_conf(ConfigType::IslandConfig);
    Log::logger(Log::Debug, "Initialize Island successfully");

    Island::init(island_conf);
    Log::logger(Log::Debug, "Initialize Island successfully");

    Wayland::init();
    Log::logger(Log::Debug, "Wayland initialized successfully");

    Renderer::init();
    Log::logger(Log::Debug, "Renderer initialized successfully");

    Timer::init();
    Log::logger(Log::Debug, "Timer initialized successfully");

    Wayland::set_report_click(Object::click);
    Wayland::set_need_draw(Island::request_redraw);

    Wayland::apply_config(
       state->island_width,
       state->island_height,
       state->zone,
       state->anchor_top
    );

    Log::logger(Log::Debug, "Initialization completed successfully");
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