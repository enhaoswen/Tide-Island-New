#pragma once

#include <cstdint>
#include <limits>
#include <string>

struct Frame {
    float x, y, width, height = std::numeric_limits<float>::max();

    bool operator==(const Frame&) const = default;
};

struct Position {
    float x, y;
};

// Normalized source rectangle of a texture.
struct UvRect {
    float u0{0}, v0{0}, u1{1}, v1{1};

    bool operator==(const UvRect&) const = default;
};

struct FontHandle {
    std::uint64_t id{};
};

struct Font {
    std::string path;
    std::uint16_t size{};
};

enum struct StateType : std::uint8_t {
    Clock,
    Lyrics,
    MusicPLayer,
    ControlCenter,
    Osd,
};
