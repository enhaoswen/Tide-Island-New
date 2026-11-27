#pragma once

#include <string>

#include <glaze/core/custom.hpp>
#include <glaze/core/meta.hpp>

#include "utils/color.hpp"

/// @brief Glaze reflection specialization serializing Color as a #RRGGBBAA string.
template<>
struct glz::meta<Color> {
	static constexpr auto read_color_ = [](Color& color, const std::string& input, glz::context& ctx) {
		const auto parsed{Color::parse(input)};
		if(!parsed) {
			ctx.error                = glz::error_code::syntax_error;
			ctx.custom_error_message = parsed.error();
			return;
		}
		color = *parsed;
	};

	static constexpr auto write_color_ = [](const Color& color) { return color.to_string(); };

	static constexpr auto value{glz::custom<read_color_, write_color_>};
};
