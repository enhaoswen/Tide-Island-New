#pragma once

#include <cstdint>
#include <filesystem>
#include <source_location>

namespace Config {

enum class ConfigType : char {
    TideConfig,
};

struct TideConfig {
    std::uint16_t island_width{};
    std::uint16_t island_height{};
};

std::filesystem::path get_path(ConfigType type);

template <typename T>
std::filesystem::path get_path(std::source_location l);

bool exist(ConfigType type);

template <typename T>
bool read(T& t, std::source_location l = std::source_location::current());

template <typename T>
bool write(const T& t, std::source_location l = std::source_location::current());

}