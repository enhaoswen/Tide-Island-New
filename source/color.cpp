#include "color.hpp"
#include "log.hpp"

#include <string_view>
#include <source_location>
#include <type_traits>

using namespace std;
using RGBA = array<float, 4>;

namespace {

RGBA default_color = {0,0,0,1};

template<typename T, size_t N>
string array_to_string(const array<T, N>& arr) {
    string result;

    for (size_t i = 0; i < N; ++i) {
        if (i)
            result += ',';

        result += format("{}", arr[i]);
    }

    return result;
}

RGBA hex_to_rgba(string_view str, source_location l = source_location::current()) {
    auto error = [&](string_view msg) {
        Log::logger(
            Log::Error,
            R"@({}:{}: {}, value="{}")@",
            l.file_name(),
            l.line(),
            msg,
            str
        );
        return default_color;
    };

    if (str.size() != 7 && str.size() != 9)
        return error("Color must be #RRGGBB or #RRGGBBAA");

    if (str[0] != '#')
        return error("Color must start with '#'");

    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    auto byte = [&](size_t pos) -> int {
        int high = hex(str[pos]);
        int low  = hex(str[pos + 1]);

        if (high < 0 || low < 0)
            return -1;

        return high * 16 + low;
    };

    int r = byte(1);
    int g = byte(3);
    int b = byte(5);
    int a = str.size() == 9 ? byte(7) : 255;

    if (r < 0 || g < 0 || b < 0 || a < 0)
        return error("Color contains invalid hexadecimal digits");

    return {
        r / 255.0f,
        g / 255.0f,
        b / 255.0f,
        a / 255.0f
    };
}

} // namespace



Color::Color(string_view arg_color, source_location l) {
    color = hex_to_rgba(arg_color, l);
}

Color::Color(array<float, 3> arg_color, source_location l) {

    for (float f : arg_color) {
        if (f > 1 || f < 0) {
            Log::logger(
                Log::Error,
                R"@({}:{}: Unsupported color type, value="{}")@",
                l.file_name(),
                l.line(),
                array_to_string(arg_color)
            );
            color = default_color;
            return;
        }
    }

    color = {arg_color[0], arg_color[1], arg_color[2], 1};
}

Color::Color(array<float, 4> arg_color, source_location l) {
    for (float f : arg_color) {
        if (f > 1 || f < 0) {
            Log::logger(
                Log::Error,
                R"@({}:{}: Unsupported color type, value="{}")@",
                l.file_name(),
                l.line(),
                array_to_string(arg_color)
            );
            color = default_color;
            return;
        }
    }

    color = arg_color;

}
