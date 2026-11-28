#include "app/settings.hpp"

#include "backend/config.hpp"

namespace {

Settings::AppSettings settings{};

Config::TideConfig to_config() {
	Config::TideConfig config{};
	config.fps = settings.fps_;
	return config;
}

}

namespace Settings {

const AppSettings& get_settings() {
	return settings;
}

void init() {
	Config::TideConfig config{to_config()};
	if(!Config::read(config))
		return;

	settings.fps_ = config.fps;
	Config::write(config);
}

}
