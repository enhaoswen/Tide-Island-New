#include "app/settings.hpp"

#include "backend/config.hpp"

namespace {

Settings::AppSettings settings{};

}

namespace Settings {

const AppSettings& get_settings() {
	return settings;
}

void init() {
	const Config::TideConfig& config{Config::get_config()};
	settings.fps_     = config.fps;
	settings.is_blur_ = config.blur;
	settings.is_24h_  = config.twenty_four_h;
}

}
