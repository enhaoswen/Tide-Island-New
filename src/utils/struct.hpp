#pragma once

#include "color.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>

using ClickCallback = std::function<void()>;

// Positions and sizes use floating point values for animation.
struct Frame {
    float x, y, width, height = std::numeric_limits<float>::max();

    bool operator==(const Frame&) const = default;
};

enum struct Align : std::uint8_t {
    Left,
    Center,
    Right,
};

// Fonts.

struct FontHandle {
    std::uint64_t id{};
};

struct Font {
    std::string   path{};
    std::uint16_t size{};
};

// Scene descriptors.

struct RectDesc {
    Frame frame{};
    float radius{};
    Color color{};
};

struct ImageDesc {
    Frame       frame{};
    std::string path{};
    float       radius{};
    Align       horizontal_align{};
    Align       vertical_align{};

    // If true, the image is scaled proportionally to fit the given size.
    // If false, the image keeps its original size and may be cropped.
    bool        fit{true};
};

struct TextDesc {
    Frame       frame{};
    std::string text{};
    FontHandle  font{};
    Color       color{};
    Align       horizontal_align{};
    Align       vertical_align{};
    bool        fit{true};
};

struct LayoutDesc {
    Frame frame{};
    float radius{};
    float spacing{};
    Color background{0, 0, 0, 0};
    Align cross_align{Align::Center};
    bool  vertical{};
    bool  fit{true};
};

// Animation.

enum struct AnimationTarget : std::uint8_t {
    Width,
    Height,
    X,
    Y,
    Radius,
    ColorR,
    ColorG,
    ColorB,
    ColorA,
};

struct AnimationDesc {
    float           to{};
    float           priority{};
    std::uint32_t   duration{}; // Milliseconds.
    AnimationTarget target{};
};

// Runtime state for an animation.
struct Animation {
    float                                 from{};
    float                                 to{};
    float*                                target_ptr{};
    float                                 priority{};
    std::uint32_t                         duration{}; // Milliseconds.
    std::chrono::steady_clock::time_point start_time{};
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