#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace LyricsMpris::Detail {

/// @brief Trims ASCII whitespace from both ends of text.
inline std::string_view trim(std::string_view text) {
	constexpr auto k_whitespace{" \t\r\n\f\v"};
	const auto     first{text.find_first_not_of(k_whitespace)};
	if(first == text.npos)
		return {};
	const auto last{text.find_last_not_of(k_whitespace)};
	return text.substr(first, last - first + 1);
}

/// @brief Parses a numeric value from text.
template<class T>
std::optional<T> number(std::string_view text) {
	text = trim(text);
	if(text.starts_with('+'))
		text.remove_prefix(1);
	T result{};
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
	if(error != std::errc{} || end != text.data() + text.size())
		return {};
	return result;
}

/// @brief Returns true when the name is an MPRIS service name.
inline bool is_service(std::string_view name) {
	return name.starts_with("org.mpris.MediaPlayer2.");
}

}
