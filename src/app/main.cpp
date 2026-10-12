#include "app/API.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"

#include <chrono>
#include <format>

using namespace std;

namespace {

Object::Text* clock_text{};

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
    API::add_rectangle(main_land_desc);
}

void set_clock_state() {
    auto now = chrono::system_clock::now();
    auto next = floor<chrono::minutes>(now) + chrono::minutes{1};
    string time = format("{:%H:%M}", chrono::current_zone()->to_local(now));

    const IslandConf& island_conf = API::island_state();

    if (!clock_text) {
        TextDesc clock_desc {
            .frame = {0, 0, island_conf.island_width, island_conf.island_height},
            .text = time,
            .font = API::load_font("Sans Serif", 18),
            .color = Color::White,
            .horizontal_align = Align::Center,
            .vertical_align = Align::Center,
        };
        clock_text = API::add_text(clock_desc);
    }
    else {
        clock_text->set_text(time);
        clock_text->set_frame({0, 0, island_conf.island_width, island_conf.island_height});
    }

    auto deadline = duration_cast<chrono::microseconds>(next - chrono::system_clock::now());
    API::push_event(deadline, set_clock_state,0);
}

int main() {
    println("");

    frame_logger(Log::Debug,
        "This build was compiled in debug mode.",
        "Performance may be reduced and additional debug output may appear."
    );

    API::init();

    draw_main_land();
    set_clock_state();

    API::run();
    clock_text = nullptr;
    API::shutdown();
    return 0;
}
