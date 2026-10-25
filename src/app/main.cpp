#include "app/API.hpp"
#include "scene/object.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"
#include "state_manager.hpp"

using namespace std;

namespace {

Object::Handle<Object::Layout> add_root() {
    const auto& island = API::get_island();

    LayoutDesc root_desc = {
        .frame   = {0, 0, static_cast<float>(island.island_width), static_cast<float>(island.island_height)},
        .spacing = 0,
    };

    return Object::add_layout(root_desc);
}

Object::Handle<Object::ClickableRect> add_main_land() {
    const auto& island = API::get_island();

    const RectDesc main_land_desc{
        .frame  = {0, 0, static_cast<float>(island.island_width), static_cast<float>(island.island_height)},
        .radius = island.radius,
        .color  = island.color,
    };
    const auto main_land = Object::add_clickable_rect(main_land_desc);

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

    const auto main_font = API::load_font("Sans Serif", 18);
    auto       main_land = add_main_land();
    auto       root      = add_root();

    StateManager::Resources resources{
        .main_font = main_font,
        .root      = root,
        .main_land = main_land,
    };

    StateManager::init(resources);

    StateManager::switch_state(StateType::Clock);

    API::run();
    API::shutdown();
    return 0;
}