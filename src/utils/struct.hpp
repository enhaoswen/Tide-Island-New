#pragma once

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

struct FontHandle {
    std::uint64_t id{};
};

struct Font {
    std::string   path{};
    std::uint16_t size{};
};

enum struct StateType : std::uint8_t {
    Clock,
    Lyrics,
    MusicPLayer,
    ControlCenter,
};

