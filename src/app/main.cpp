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

    print(
        R"_(


    ooooooooooooo  o8o        .o8                 ooooo          oooo                              .o8
    8'   888   `8  `"'       "888                 `888'          `888                             "888
         888      oooo   .oooo888   .ooooo.        888   .oooo.o  888   .oooo.   ooo. .oo.    .oooo888
         888      `888  d88' `888  d88' `88b       888  d88(  "8  888  `P  )88b  `888P"Y88b  d88' `888
         888       888  888   888  888ooo888       888  `"Y88b.   888   .oP"888   888   888  888   888
         888       888  888   888  888    .o       888  o.  )88b  888  d8(  888   888   888  888   888
        o888o     o888o `Y8bod88P" `Y8bod8P'      o888o 8""888P' o888o `Y888""8o o888o o888o `Y8bod88P"

)_"
    );


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
