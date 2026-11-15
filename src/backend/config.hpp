#pragma once

#include "utils/color.hpp"

#include <cstdint>
#include <filesystem>
#include <source_location>

namespace Config {

struct IslandConfig {
    std::uint16_t island_width{};
    std::uint16_t island_height{};
    std::int16_t  zone{};
    std::int16_t  anchor_top{};
    float         radius{};
    Color         color{};
};

[[nodiscard]] std::filesystem::path get_path();

[[nodiscard]] bool exists();

template <typename T>
bool read(T& t, std::source_location l = std::source_location::current());

template <typename T>
bool write(const T& t, std::source_location l = std::source_location::current());

extern template bool read<IslandConfig>(IslandConfig&, std::source_location);

extern template bool write<IslandConfig>(const IslandConfig&, std::source_location);

}
