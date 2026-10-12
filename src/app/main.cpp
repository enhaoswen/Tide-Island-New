#include "app/API.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"
#include "scene/layout.hpp"

#include <chrono>
#include <format>

using namespace std;

namespace {
}

void draw_main_land() {

    const IslandConf& island_conf = API::island_state();
    RectDesc main_land_desc{
        .frame = { 0, 0, island_conf.island_width, island_conf.island_height},
        .radius = island_conf.radius,
        .color = island_conf.color,
        .click_callback_left = []() {
            Log::logger(Log::Debug, "Left click callback triggered");
        },
        .click_callback_right = []() {
            Log::logger(Log::Debug, "Right click callback triggered");
        },
    };
    API::draw_rectangle(main_land_desc);
}

void set_clock_state() {
    auto now = chrono::system_clock::now();
    auto next = floor<chrono::minutes>(now) + chrono::minutes{1};
    string time = format("{:%H:%M}", chrono::current_zone()->to_local(now));

    const IslandConf& island_conf = API::island_state();

    API::clear_status();

    TextDesc clock_desc {
        .frame = {0, 0, island_conf.island_width, island_conf.island_height},
        .text = time,
        .font = API::load_font("Sans Serif", 18),
        .color = Color::White,
        .horizontal_align = Align::Center,
        .vertical_align = Align::Center,
    };

    API::draw_text(clock_desc);
    
    chrono::microseconds deadline = duration_cast<chrono::microseconds>(next - now);
    API::push_event(deadline, set_clock_state,0);
}

int main() {
    println("");

    frame_logger(Log::Debug,
        "This build was compiled in debug mode.",
        "Performance may be reduced and additional debug output may appear."
    );

    API::init();

    const IslandConf& island_conf = API::island_state();


    ImageDesc img_desc {
        .frame = {0, 0, island_conf.island_width, island_conf.island_height},
        .radius = 0,
        .path = "/home/swen/Downloads/images.jpeg",
        .horizontal_align = Align::Center,
        .vertical_align = Align::Center,
        .click_callback_left = []() {
            Log::logger(Log::Debug, "Left click callback triggered");
        },
        .click_callback_right = []() {
            Log::logger(Log::Debug, "Right click callback triggered");
        },
    };

    set_clock_state();

    API::run();
    return 0;
}
