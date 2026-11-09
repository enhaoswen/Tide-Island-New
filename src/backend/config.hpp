#pragma once

#include <cstdint>
#include <source_location>

namespace Config {

enum class ConfigType : char {
    TideConfig,
};

struct TideConfig {
    std::uint16_t island_width{};
    std::uint16_t island_height{};
};

template <typename T>
bool read(T& t, std::source_location l = std::source_location::current());

template <typename T>
bool write(const T& t, std::source_location l = std::source_location::current());

}