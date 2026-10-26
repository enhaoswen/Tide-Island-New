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
        .background = island.color,
        .radius = island.radius,
    };
    const auto root = Object::add_layout(root_desc);

    root->set_callback_left([]() {
        StateManager::switch_state(StateType::MusicPLayer);
    });

    return root;
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
    auto       root      = add_root();

    StateManager::Resources resources{
        .main_font = main_font,
        .root      = root,
    };

    StateManager::init(resources);

    StateManager::switch_state(StateType::Clock);

    API::run();
    API::shutdown();
    return 0;
}
