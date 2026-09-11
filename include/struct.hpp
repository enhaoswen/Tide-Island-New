#pragma once

#include <array>
#include <chrono>
#include <vector>
#include <string>
#include <variant>
#include <unordered_map>

// Remember to change `types` in `create_str_config` if you add new types to the config variant. (config.cpp)

using config = std::unordered_map<
    std::string, 
    std::variant<
        int, 
        float, 
        std::string, 
        bool, 
        std::vector<float>, 
        std::vector<int>, 
        std::vector<std::string>,
        std::array<float, 4>
>>;

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
    float island_width{};
    float island_height{};
    int zone{-1};
    float anchor_top{};
    float radius{};

    // DO NOT init need_redraw, it should always be true in the beginning.
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

enum struct ConfigType : char {
    IslandConfig,

    Count // DO NOT use, just to get the count of config types for loop.
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
