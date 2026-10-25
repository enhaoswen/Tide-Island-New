#include "app/API.hpp"
#include "scene/object.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"
#include "state_manager.hpp"

using namespace std;

namespace {

Object::Handle<Object::ClickableRect> add_main_land() {
    const auto& island = API::get_island();
    constexpr std::uint16_t expanded_width{400};

    constexpr AnimationDesc main_land_w{
        .to       = static_cast<float>(expanded_width),
        .priority = 1,
        .duration = 300,
        .target   = AnimationTarget::Width,
    };

    const RectDesc main_land_desc{
        .frame  = {0, 0, static_cast<float>(island.island_width), static_cast<float>(island.island_height)},
        .radius = island.radius,
        .color  = island.color,
    };
    const auto main_land = Object::add_clickable_rect(main_land_desc);

    main_land->set_callback_right([]() {
        Log::logger(Log::Debug, "Right click callback triggered");
    });
    main_land->set_callback_left([main_land, main_land_w]() {
        API::resize(expanded_width, API::get_island().island_height);
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
