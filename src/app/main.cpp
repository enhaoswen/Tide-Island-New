#include "app/API.hpp"
#include "app/views/clock_view.hpp"
#include "backend/egl_vendor.hpp"
#include "scene/object.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"
#include <chrono>

using namespace std;

namespace {

Object::Rectangle* add_main_land() {
    const auto& island_conf = API::island_state();

    AnimationDesc main_land_w = {
        .to       = 400,
        .priority = 1,
        .duration = 300ms,
        .target   = AnimationTarget::Width,
    };

    RectDesc main_land_desc{
        .frame                = {0, 0, island_conf.island_width, island_conf.island_height},
        .radius               = island_conf.radius,
        .color                = island_conf.color,
        .click_callback_right = []() {
            Log::logger(Log::Debug, "Right click callback triggered");
        },
    };
    Object::Rectangle* main_land = API::add_rectangle(main_land_desc);

    main_land->set_click_callback_left([main_land, main_land_w]() {
        API::resize(main_land_w.to, main_land->frame().height);
        main_land->add_animation(main_land_w);
    });

    return main_land;

}

} // namespace

int main() {

    println("");

    frame_logger(Log::Debug,
        "This build was compiled in debug mode.",
        "Performance may be reduced and additional debug output may appear."
        );

    EglVendor::select_for_wayland();

    API::init();

    Object::Rectangle* main_land = add_main_land();
    // ClockView::start();

    API::run();
    ClockView::stop();
    API::shutdown();
    return 0;
}
