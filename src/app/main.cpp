#include "app/API.hpp"
#include "app/views/clock_view.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"

using namespace std;

namespace {

void add_main_land() {
    const auto& island_conf = API::island_state();

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

} // namespace

int main() {
    println("");

    frame_logger(Log::Debug,
        "This build was compiled in debug mode.",
        "Performance may be reduced and additional debug output may appear."
    );

    API::init();

    add_main_land();
    ClockView::start();

    API::run();
    ClockView::stop();
    API::shutdown();
    return 0;
}
