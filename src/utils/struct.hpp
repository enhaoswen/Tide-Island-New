#pragma once

#include "color.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>

struct Frame {
    float x, y, width, height = std::numeric_limits<float>::max();

    bool operator==(const Frame&) const = default;
};

struct Position {
    float x, y;
};

// Fonts.

struct FontHandle {
    std::uint64_t id{};
};

struct Font {
    std::string   path{};
    std::uint16_t size{};
};


// Application state.

enum struct StateType : std::uint8_t {
    Clock,
    Lyrics,
    MusicPLayer,
};

struct IslandConf {
    std::uint16_t island_width{};
    std::uint16_t island_height{};
    std::int16_t  zone{-1};
    std::int16_t  anchor_top{};
    float         animation_scale{1};
    float         radius{};
    float         current_priority{};
    Color         color{};
    bool          need_redraw{true};
    bool          is_running{true};
    StateType     state{StateType::Clock};
};