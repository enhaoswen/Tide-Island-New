#pragma once

#include <source_location>
#include <string>
#include <string_view>
#include <array>
#include <expected>

// input can be #RRGGBB or #RRGGBBAA
// But not rgb(a,b,c) or rgba(a,b,c,d)

// Ex. #AABBCC or #AABBCCDD
// but not rgb(1,2,3) or rgba(1,2,3,4)

// output is array<float,4> (RGBA), from 0 - 1. but not 0 - 255
// Ex. opaque black = {0.0, 0.0, 0.0, 1.0}
// opaque red = {1.0, 0.0, 0.0, 1.0}

class Color {
private:
    std::array<float,4> color;

    struct PresetTag {};
    constexpr Color(PresetTag, unsigned char r, unsigned char g, unsigned char b)
        : color{r / 255.0f, g / 255.0f, b / 255.0f, 1.0f} {}

public:
    static const Color Black;
    static const Color White;
    static const Color Gray;
    static const Color Silver;
    static const Color Red;
    static const Color Maroon;
    static const Color Yellow;
    static const Color Olive;
    static const Color Lime;
    static const Color Green;
    static const Color Cyan;
    static const Color Teal;
    static const Color Blue;
    static const Color Navy;
    static const Color Magenta;
    static const Color Purple;

    static std::expected<Color, std::string_view> parse(std::string_view input);

    Color();
    Color(std::string_view color, std::source_location l = std::source_location::current());
    Color(std::array<float,3> arg_color, std::source_location l = std::source_location::current());
    Color(std::array<float,4> arg_color, std::source_location l = std::source_location::current());
    Color(float r, float g, float b, float a, std::source_location l = std::source_location::current());
    Color(float r, float g, float b, std::source_location l = std::source_location::current());
    std::string to_string() const;
    constexpr std::array<float, 4> to_rgba() const { return color; }
};

inline constexpr Color Color::Black{PresetTag{}, 0, 0, 0};
inline constexpr Color Color::White{PresetTag{}, 255, 255, 255};
inline constexpr Color Color::Gray{PresetTag{}, 128, 128, 128};
inline constexpr Color Color::Silver{PresetTag{}, 192, 192, 192};
inline constexpr Color Color::Red{PresetTag{}, 255, 0, 0};
inline constexpr Color Color::Maroon{PresetTag{}, 128, 0, 0};
inline constexpr Color Color::Yellow{PresetTag{}, 255, 255, 0};
inline constexpr Color Color::Olive{PresetTag{}, 128, 128, 0};
inline constexpr Color Color::Lime{PresetTag{}, 0, 255, 0};
inline constexpr Color Color::Green{PresetTag{}, 0, 128, 0};
inline constexpr Color Color::Cyan{PresetTag{}, 0, 255, 255};
inline constexpr Color Color::Teal{PresetTag{}, 0, 128, 128};
inline constexpr Color Color::Blue{PresetTag{}, 0, 0, 255};
inline constexpr Color Color::Navy{PresetTag{}, 0, 0, 128};
inline constexpr Color Color::Magenta{PresetTag{}, 255, 0, 255};
inline constexpr Color Color::Purple{PresetTag{}, 128, 0, 128};
