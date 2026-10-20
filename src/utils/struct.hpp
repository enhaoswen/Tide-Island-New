#pragma once

#include "color.hpp"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

// This file used to put the structure that developer might need in API.cpp / main.cpp.
// because some struct / enum might not get displaied by clangd if their header is not
// included directly.

// Ex. we need RectDesc in main.cpp
// but we did not include renderer.hpp because it's included by API.cpp already, and we
// don't want to include it again in main.cpp because it might looks messy.
// so we just put all of them in struct.hpp

// width and height must be float, because we need to support animation, 
// and the value might be float during animation

using ClickCallback = std::function<void()>;

struct Frame {
    float x, y, width, height;
};

enum struct Align : char {
    Left,
    Center,
    Right,
};

enum struct AnimationTarget : char {
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
    uint32_t        duration{};

    // we keep it nullptr when submit to object.animation
    float*          target_ptr{};
    AnimationTarget target{};
};

// 32B
struct Animation {
    float                                 from{};
    float                                 to{};
    float*                                target_ptr{};
    float                                 priority{};
    uint32_t                              duration{};
    std::chrono::steady_clock::time_point start_time;
};

struct FontHandle {
    std::uint64_t id{};
};


struct RectDesc {
    Frame                  frame{};
    float                  radius{};
    Color                  color{};
    std::vector<Animation> animations{};
};

struct ImageDesc {
    Frame                  frame{};
    float                  radius{};
    std::string            path;
    std::vector<Animation> animations{};
    Align                  horizontal_align;
    Align                  vertical_align;
};

struct TextDesc {
    Frame                  frame{};
    std::string            text;
    FontHandle             font;
    Color                  color;
    Align                  horizontal_align;
    Align                  vertical_align;
    std::vector<Animation> animations{};
};

// About Priority
// Priority used to implement the right callback when 2 events are triggered at the same time.
// We always implement the callback with the highest priority, and ignore the other one.

// Clock, Lyrics, Custom Page: 0

struct Event {
    std::chrono::steady_clock::time_point deadline;

    void (*callback)(){};

    float priority{};
};

struct IslandConf {
    float island_width{};
    float island_height{};
    int   zone{-1};
    int   anchor_top{};
    float animation_scale{1};
    float radius{};
    float current_priority{};
    Color color;
    bool  need_redraw{true};
    bool  is_running{true};
};

struct Font {
    std::string path;
    size_t      size;
};