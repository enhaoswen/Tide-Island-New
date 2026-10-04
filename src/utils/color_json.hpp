#pragma once

#include "utils/color.hpp"

#include <glaze/core/custom.hpp>
#include <glaze/core/meta.hpp>

template <>
struct glz::meta<Color> {
    static constexpr auto read_color = [](Color& color, const std::string& input,
                                          glz::context& ctx) {
        auto parsed = Color::parse(input);
        if (!parsed) {
            ctx.error = glz::error_code::syntax_error;
            ctx.custom_error_message = parsed.error();
            return;
        }
        color = *parsed;
    };

    static constexpr auto write_color = [](const Color& color) {
        return color.to_string();
    };

    static constexpr auto value = glz::custom<read_color, write_color>;
};
