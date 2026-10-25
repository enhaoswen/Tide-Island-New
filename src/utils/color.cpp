#include "utils/color.hpp"
#include "utils/log.hpp"

#include <string_view>
#include <source_location>
#include <cmath>

using namespace std;
using RGBA = array<float, 4>;

namespace {

template<typename T, size_t N>
string array_to_string(const array<T, N>& arr) {
    string result{};

    for (size_t i{}; i < N; ++i) {
        if (i)
            result += ',';

        result += format("{}", arr[i]);
    }

    return result;
}

expected<RGBA, string_view> hex_to_rgba(string_view str) {
    if (str.size() != 7 && str.size() != 9)
        return unexpected("Color must be #RRGGBB or #RRGGBBAA");

    if (str[0] != '#')
        return unexpected("Color must start with '#'");

    constexpr auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    const auto byte = [&](size_t pos) -> int {
        const int high{hex(str[pos])};
        const int low{hex(str[pos + 1])};

        if (high < 0 || low < 0)
            return -1;

        return high * 16 + low;
    };

    const int r{byte(1)};
    const int g{byte(3)};
    const int b{byte(5)};
    const int a{str.size() == 9 ? byte(7) : 255};

    if (r < 0 || g < 0 || b < 0 || a < 0)
        return unexpected("Color contains invalid hexadecimal digits");

    return RGBA{
        static_cast<float>(r) / 255.0f,
        static_cast<float>(g) / 255.0f,
        static_cast<float>(b) / 255.0f,
        static_cast<float>(a) / 255.0f
    };
}

} // namespace

Color::Color(string_view arg_color, source_location l) {
    const auto parsed = hex_to_rgba(arg_color);
    if (parsed) {
        color = *parsed;
    }
    else {
        Log::logger(Log::Error, R"@({}:{}: {}, value="{}")@",
                    l.file_name(), l.line(), parsed.error(), arg_color);
    }
}

expected<Color, string_view> Color::parse(string_view input) {
    const auto parsed = hex_to_rgba(input);
    if (!parsed)
        return unexpected(parsed.error());
    Color result{};
    result.color = *parsed;
    return result;
}

Color::Color(array<float, 3> arg_color, source_location l)
    : Color(array<float, 4>{arg_color[0], arg_color[1], arg_color[2], 1}, l) {}

Color::Color(array<float, 4> arg_color, source_location l) {
    for (const float f : arg_color) {
        if (!isfinite(f) || f > 1 || f < 0) {
            Log::logger(
                Log::Error,
                R"@({}:{}: Unsupported color type, value="{}")@",
                l.file_name(),
                l.line(),
                array_to_string(arg_color)
            );
            return;
        }
    }

    color = arg_color;
}

Color::Color(
    float r,
    float g,
    float b,
    source_location l
) : Color(array<float, 4>{r, g, b, 1.0F}, l) {}

Color::Color(
    float r,
    float g,
    float b,
    float a,
    source_location l
) : Color(array<float, 4>{r, g, b, a}, l) {}

string Color::to_string() const {
    const auto byte = [](float value) {
        if (!isfinite(value)) return 0;
        return static_cast<int>(
            round(std::clamp(value, 0.0f, 1.0f) * 255.0f)
        );
    };

    return format(
        "#{:02x}{:02x}{:02x}{:02x}",
        byte(color[0]),
        byte(color[1]),
        byte(color[2]),
        byte(color[3])
    );
}
