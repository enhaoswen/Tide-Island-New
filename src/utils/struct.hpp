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
    std::string_view text;
    Color color;
    Align horizontal_align;
    Align vertical_align;
    void (*click_callback_left) () = nullptr;
    void (*click_callback_right) () = nullptr;
};

struct Event {
    std::chrono::steady_clock::time_point deadline;
    void (*callback)();
};

// Remember to change `conf_to_island_conf` in config.cpp if you changed `Island Conf`


// But make sure the value that you set is always int.
struct IslandConf {
    int island_width{};
    int island_height{};
    int zone{-1};
    int anchor_top{};
    float radius{};
    Color color{};

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
    ColorA
};

struct Animation {
    std::chrono::steady_clock::time_point start_time;
    std::chrono::milliseconds duration;
    float from;
    float to;
    AnimationTarget target;
    float* target_ptr{};
};

struct Font {
    std::string path;
    size_t size;
};
