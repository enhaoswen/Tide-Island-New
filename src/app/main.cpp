#include "app/API.hpp"
#include "app/state_manager.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"

using namespace std;

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

)_");

	frame_logger(Log::LogLevel::Debug,
		"This build was compiled in debug mode.",
		"Performance may be reduced and additional debug output may appear.");

	Api::select_for_wayland();

	Api::init();

	const StateManager::Resources resources{
		.main_font_ = Api::load_font("Sans Serif", 18, Text::Weight::Bold),
		.title_     = Api::load_font("Sans Serif", 16, Text::Weight::DemiBold),
		.artist_    = Api::load_font("Sans Serif", 14, Text::Weight::Medium),
		.time_      = Api::load_font("Sans Serif", 12, Text::Weight::Medium),
		.icon_      = Api::load_font("Sans Serif", 22, Text::Weight::Regular),
	};

	StateManager::init(resources);
	StateManager::switch_state(StateType::Clock);

	Api::run();
	Api::shutdown();
	return 0;
}
