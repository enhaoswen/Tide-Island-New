#include "utils/color.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <source_location>
#include <string_view>

#include "utils/log.hpp"

using namespace std;

namespace {

using Rgba = array<float, 4>;

template<typename T, size_t N>
string array_to_string(const array<T, N>& arr) {
	string result{};

	for(size_t i{}; i < N; ++i) {
		if(i)
			result += ',';

		result += format("{}", arr[i]);
	}

	return result;
}

expected<Rgba, string_view> hex_to_rgba(string_view str) {
	if(str.size() != 7 && str.size() != 9)
		return unexpected("Color must be #RRGGBB or #RRGGBBAA");

	if(str[0] != '#')
		return unexpected("Color must start with '#'");

	constexpr auto k_hex = [](char c) {
		if(c >= '0' && c <= '9')
			return c - '0';
		if(c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if(c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	};

	const auto byte = [&](size_t pos) {
		const int high{k_hex(str[pos])};
		const int low{k_hex(str[pos + 1])};

		if(high < 0 || low < 0)
			return -1;

		return high * 16 + low;
	};

	const int r{byte(1)};
	const int g{byte(3)};
	const int b{byte(5)};
	const int a{str.size() == 9 ? byte(7) : 255};

	if(r < 0 || g < 0 || b < 0 || a < 0)
		return unexpected("Color contains invalid hexadecimal digits");

	return Rgba{
		static_cast<float>(r) / 255.f,
		static_cast<float>(g) / 255.f,
		static_cast<float>(b) / 255.f,
		static_cast<float>(a) / 255.f,
	};
}

}

Color::Color(string_view color, source_location location) {
	const auto parsed{hex_to_rgba(color)};
	if(parsed) {
		color_ = *parsed;
	} else {
		Log::logger(Log::LogLevel::Error,
			R"_({}:{}: {}, value="{}")_",
			location.file_name(),
			location.line(),
			parsed.error(),
			color);
	}
}

expected<Color, string_view> Color::parse(string_view input) {
	const auto parsed{hex_to_rgba(input)};
	if(!parsed)
		return unexpected(parsed.error());
	Color result{};
	result.color_ = *parsed;
	return result;
}

Color::Color(array<float, 3> arg_color, source_location location) :
	Color(array<float, 4>{arg_color[0], arg_color[1], arg_color[2], 1.f}, location) {
}

Color::Color(array<float, 4> arg_color, source_location location) {
	for(const float f : arg_color) {
		if(!isfinite(f) || f > 1.f || f < 0.f) {
			Log::logger(Log::LogLevel::Error,
				R"_({}:{}: Unsupported color type, value="{}")_",
				location.file_name(),
				location.line(),
				array_to_string(arg_color));
			return;
		}
	}

	color_ = arg_color;
}

Color::Color(float r, float g, float b, source_location location) :
	Color(array<float, 4>{r, g, b, 1.f}, location) {
}

Color::Color(float r, float g, float b, float a, source_location location) :
	Color(array<float, 4>{r, g, b, a}, location) {
}

string Color::to_string() const {
	const auto byte = [](float value) {
		if(!isfinite(value))
			return 0;
		return static_cast<int>(round(clamp(value, 0.f, 1.f) * 255.f));
	};

	return format("#{:02x}{:02x}{:02x}{:02x}", byte(color_[0]), byte(color_[1]), byte(color_[2]), byte(color_[3]));
}
