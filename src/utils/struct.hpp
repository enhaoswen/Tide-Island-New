#pragma once

#include "color.hpp"

#include <chrono>
#include <string>
#include <string_view>

// This file used to put the structure that developer might need in API.cpp / main.cpp.
// because some struct / enum might not get displaied by clangd if their header is not
// included directly.

// Ex. we need RectDesc in main.cpp
// but we did not include renderer.hpp because it's included by API.cpp already, and we
// don't want to include it again in main.cpp because it might looks messy.
// so we just put all of them in struct.hpp


// width and height must be float, because we need to support animation, 
// and the value might be float during animation
struct Frame {
    float x, y, width, height;
};

enum struct Align : char{
    Left,
    Center,
    Right
};

struct FontHandle {
    std::uint64_t id{};
};

struct RectDesc {
    Frame frame{};
    float radius{};
    Color color{};
    void (*click_callback_left) () = nullptr;
    void (*click_callback_right) () = nullptr;
};

struct ImageDesc {
    Frame frame{};
    float radius{};
    std::string path;
    Align horizontal_align;
    Align vertical_align;
    void (*click_callback_left) () = nullptr;
    void (*click_callback_right) () = nullptr;

};

struct TextDesc {
    Frame frame{};
    std::string text;
    FontHandle font;
    Color color;
    Align horizontal_align;
    Align vertical_align;
    void (*click_callback_left) () = nullptr;
    void (*click_callback_right) () = nullptr;
};

// About Priority
// Priority used to implement the right callback when 2 events are triggered at the same time.
// We always implement the callback with the highest priority, and ignore the other one.

// Clock, Lyrics, Custom Page: 0

struct Event {
    std::chrono::steady_clock::time_point deadline;
    void (*callback)();
    float priority;
};

struct IslandConf {
    float island_width{};
    float island_height{};
    int zone{-1};
    int anchor_top{};
    float radius{};
    float current_priority{};
    Color color;

    bool need_redraw{true};
    bool is_running{true};
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

struct Animation {
    float from{};
    float to{};
    float* target_ptr{};
    float priority{};
    std::chrono::steady_clock::time_point start_time;
    std::chrono::milliseconds duration;
    AnimationTarget target;
};

struct Font {
    std::string path;
    size_t size;
};
