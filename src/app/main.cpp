#include "app/API.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"

using namespace std;

int main() {
    println("");

    frame_logger(Log::Debug,
        "This build was compiled in debug mode.",
        "Performance may be reduced and additional debug output may appear."
    );

    API::init();

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

    TextDesc text_desc {
        .frame = {0, 0, island_conf.island_width, island_conf.island_height},
        .text = "Hello,你好!",
        .font = API::load_font("Inter Display", 18),
        .color = {1,1,1,1},
        .horizontal_align = Align::Center,
        .vertical_align = Align::Center,
    };

    // test
    API::draw_rectangle(main_land_desc);
    //API::draw_image(img_desc);
    API::draw_text(text_desc);

    API::run();
    return 0;
}