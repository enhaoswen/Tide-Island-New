#pragma once

#include <array>
#include <chrono>
#include <string>

// This file used to put the structure that developer might need in API.cpp / main.cpp.
// because some struct / enum might not get displaied by clangd if their header is not
// included directly.
//
// Ex. we need RectDesc in main.cpp
// but we did not include renderer.hpp because it's included by API.cpp already, and we
// don't want to include it again in main.cpp because it might looks messy.
// so we just put all of them in struct.hpp

struct Frame {
    int x, y, width, height;
};

enum struct Align : char{
    Left,
    Center,
    Right
};

struct RectDesc {
    Frame frame{};
    float radius{};
    std::array<float, 4> color{};
    void (*click_callback_left) () = nullptr;
    void (*click_callback_right) () = nullptr;
};

struct ImageDesc {
    Frame frame{};
    float radius{};
    std::string path;
    void (*click_callback_left) () = nullptr;
    void (*click_callback_right) () = nullptr;
    Align horizontal_align;
    Align vertical_align;
};

struct Event {
    std::chrono::steady_clock::time_point deadline;
    void (*callback)();
};

// Remember to change `conf_to_island_conf` in config.cpp if you changed `Island Conf`

struct IslandConf {
    std::array<float, 4> color{0,0,0,1};
    int island_width{};
    int island_height{};
    int zone{-1};
    float anchor_top{};
    float radius{};

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
};

struct Font {
    std::string path;
    size_t size;
};
