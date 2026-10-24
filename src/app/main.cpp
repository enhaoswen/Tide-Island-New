#include "app/API.hpp"
#include "scene/object.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"
#include "state_manager.hpp"

using namespace std;

namespace {

Object::Handle<Object::ClickableRect> add_main_land() {
    const auto& island = API::get_island();

    AnimationDesc main_land_w = {
        .to       = 400,
        .priority = 1,
        .duration = 300,
        .target   = AnimationTarget::Width,
    };

    RectDesc main_land_desc{
        .frame  = {0, 0, island.island_width, island.island_height},
        .radius = island.radius,
        .color  = island.color,
    };
    auto main_land = Object::add_clickable_rect(main_land_desc);

    main_land->set_callback_right([]() {
        Log::logger(Log::Debug, "Right click callback triggered");
    });
    main_land->set_callback_left([main_land, main_land_w]() {
        API::resize(main_land_w.to, API::get_island().island_height);
        if (main_land) main_land->add_animation(main_land_w);
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

    API::select_for_wayland();


    API::init();

    add_main_land();

    StateManager::switch_state(StateType::Clock);

    API::run();
    API::shutdown();
    return 0;
}