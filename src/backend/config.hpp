#pragma once

#include <cstdint>
#include <filesystem>
#include <source_location>

#include "utils/color.hpp"

namespace Config {

/// @brief Island configuration persisted as JSON; member names are the serialized keys.
struct IslandConfig {
	uint16_t island_width{};
	uint16_t island_height{};
	int16_t  zone{};
	int16_t  anchor_top{};
	float    radius{};
	Color    color{};
	bool     blur{};
};

/// @brief Returns the path to the user's config file.
[[nodiscard]] std::filesystem::path get_path();

/// @brief Returns true if the config file exists.
[[nodiscard]] bool has_config();

/// @brief Reads JSON from the config file into t; returns true on success.
template<typename T>
bool read(T& t, std::source_location location = std::source_location::current());

/// @brief Merges t into the config file as JSON; returns true on success.
template<typename T>
bool write(const T& t, std::source_location location = std::source_location::current());

extern template bool read<IslandConfig>(IslandConfig&, std::source_location);

extern template bool write<IslandConfig>(const IslandConfig&, std::source_location);

}
