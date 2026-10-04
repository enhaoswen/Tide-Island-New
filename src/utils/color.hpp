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

public:
    Color();
    Color(std::string_view color, std::source_location l = std::source_location::current());
    Color(std::array<float,3> arg_color, std::source_location l = std::source_location::current());
    Color(std::array<float,4> arg_color, std::source_location l = std::source_location::current());
    Color(float r, float g, float b, float a, std::source_location l = std::source_location::current());
    Color(float r, float g, float b, std::source_location l = std::source_location::current());
    static std::expected<Color, std::string_view> parse(std::string_view input);
    std::string to_string() const;
    std::array<float, 4> to_rgba() const;
};
