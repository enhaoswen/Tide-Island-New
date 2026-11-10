#pragma once

#include "utils/color.hpp"

#include <cstdint>
#include <filesystem>
#include <source_location>

namespace Config {

enum class ConfigType : char {
    IslandConfig,
};

struct IslandConfig {
    std::uint16_t island_width{};
    std::uint16_t island_height{};
    std::int16_t zone{};
    std::int16_t anchor_top{};
    float radius{};
    Color color{};
};

std::filesystem::path get_path(ConfigType type);

template <typename T>
std::filesystem::path get_path(std::source_location l = std::source_location::current());

bool exist(ConfigType type);

template <typename T>
bool read(T& t, std::source_location l = std::source_location::current());

template <typename T>
bool write(const T& t, std::source_location l = std::source_location::current());

extern template std::filesystem::path get_path<IslandConfig>(std::source_location);

extern template bool read<IslandConfig>(IslandConfig&, std::source_location);

extern template bool write<IslandConfig>(const IslandConfig&, std::source_location);

}