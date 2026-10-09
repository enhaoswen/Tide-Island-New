#include "utils/color.hpp"
#include "utils/log.hpp"

#include <string_view>
#include <source_location>
#include <cmath>

using namespace std;
using RGBA = array<float, 4>;

namespace {

// RGB values in Color::Preset order; preset colors are fully opaque.
constexpr array<array<unsigned char, 3>, 16> preset_colors{{
    {  0,   0,   0}, // Black
    {255, 255, 255}, // White
    {128, 128, 128}, // Gray
    {192, 192, 192}, // Silver
    {255,   0,   0}, // Red
    {128,   0,   0}, // Maroon
    {255, 255,   0}, // Yellow
    {128, 128,   0}, // Olive
    {  0, 255,   0}, // Lime
    {  0, 128,   0}, // Green
    {  0, 255, 255}, // Cyan
    {  0, 128, 128}, // Teal
    {  0,   0, 255}, // Blue
    {  0,   0, 128}, // Navy
    {255,   0, 255}, // Magenta
    {128,   0, 128}, // Purple
}};

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

expected<RGBA, string_view> hex_to_rgba(string_view str) {
    if (str.size() != 7 && str.size() != 9)
        return unexpected("Color must be #RRGGBB or #RRGGBBAA");

    if (str[0] != '#')
        return unexpected("Color must start with '#'");

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
        return unexpected("Color contains invalid hexadecimal digits");

    return RGBA{
        r / 255.0f,
        g / 255.0f,
        b / 255.0f,
        a / 255.0f
    };
}

} // namespace

Color::Color() : Color(Preset::Black) {}

Color::Color(Preset preset, source_location l) {
    const auto index = static_cast<unsigned char>(preset);
    if (index >= preset_colors.size()) {
        Log::logger(Log::Error, "{}:{}: Invalid color preset {}",
                    l.file_name(), l.line(), static_cast<unsigned>(index));
        color = Color{Preset::Black}.to_rgba();
        return;
    }
    const auto& rgb = preset_colors[index];
    color = {rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f};
}

Color::Color(string_view arg_color, source_location l) {
    auto parsed = hex_to_rgba(arg_color);
    if (parsed) {
        color = *parsed;
    }
    else {
        Log::logger(Log::Error, R"@({}:{}: {}, value="{}")@",
                    l.file_name(), l.line(), parsed.error(), arg_color);
        color = Color{Preset::Black}.to_rgba();
    }
}

expected<Color, string_view> Color::parse(string_view input) {
    auto parsed = hex_to_rgba(input);
    if (!parsed)
        return unexpected(parsed.error());
    Color result;
    result.color = *parsed;
    return result;
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
            color = Color{Preset::Black}.to_rgba();
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
            color = Color{Preset::Black}.to_rgba();
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
    auto byte = [](float value) {
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

array<float,4> Color::to_rgba() const {
    return color;
}
