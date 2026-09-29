#pragma once

#include "struct.hpp"

#include <source_location>
#include <string>
#include <string_view>
#include <array>

// input can be #RGB or #RGBA
// But not rgb(a,b,c) or rgba(a,b,c,d)

// Ex. #AABBCC or #AABBCCDD
// but not rgb(1,2,3) or rgba(1,2,3,4)

// output is array<float,4> (RGBA), from 0 - 1. but not 0 - 255
// Ex. black = {0.0, 0.0, 0.0, 0.0}
// red = {1.0, 0.0, 0.0, 0.0}

class Color {
private:
    std::array<float,4> color;

public:
    Color(std::string_view color, std::source_location l = std::source_location::current());
    Color(std::array<float,3> arg_color, std::source_location l = std::source_location::current());
    Color(std::array<float,4> arg_color, std::source_location l = std::source_location::current());
    std::string to_string();
    std::array<float, 4> to_rgba();
};