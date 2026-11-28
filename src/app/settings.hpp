#pragma once

#include <cstdint>

namespace Settings {

/// @brief Application-wide settings loaded from the config file.
struct AppSettings {
	uint16_t fps_{}; // frame cap; 0 = uncapped
	bool     is_blur_{false};
};

/// @brief Returns the loaded application settings.
[[nodiscard]] const AppSettings& get_settings();

/// @brief Loads the settings from the config file, writing the defaults if missing.
void init();

}
