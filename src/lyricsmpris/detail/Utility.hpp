#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace lyricsmpris::detail {

inline std::string_view trim(std::string_view text) noexcept {
    constexpr auto whitespace = " \t\r\n\f\v";
    const auto first = text.find_first_not_of(whitespace);
    if (first == text.npos) return {};
    const auto last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

template<class T>
std::optional<T> number(std::string_view text) noexcept {
    text = trim(text);
    if (text.starts_with('+')) text.remove_prefix(1);
    T result{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size()) return {};
    return result;
}

inline bool is_service(std::string_view name) noexcept {
    return name.starts_with("org.mpris.MediaPlayer2.");
}

} // namespace lyricsmpris::detail
