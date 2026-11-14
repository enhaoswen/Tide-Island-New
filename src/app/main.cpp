#include "state_manager.hpp"
#include "app/API.hpp"
#include "scene/context.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"

using namespace std;

namespace {


}

int main() {

    // init

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

    // prepare resource

    const auto main_font = API::load_font("Sans Serif", 18);

    const MusicPlayerView::Fonts player_fonts{
        .title  = API::load_font("Sans Serif", 16),
        .artist = API::load_font("Sans Serif", 14),
        .time   = API::load_font("Sans Serif", 12),
        .icon   = API::load_font("Sans Serif", 22),
    };

    StateManager::init({main_font, player_fonts});

    StateManager::switch_state(StateType::Clock);

    API::run();
    API::shutdown();
    return 0;
}