#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lyricsmpris::detail {

std::string normalize(std::string_view text, bool title = false);
std::string clean_text(std::string_view text);
std::vector<std::string> tokens(std::string_view normalized);
double similarity(std::string_view left, std::string_view right, bool title);
bool contains_tokens(std::string_view needle, std::string_view haystack);
bool comparable_script(std::string_view left, std::string_view right);
unsigned version_flags(std::string_view value);
bool valid_utf8(std::string_view value) noexcept;

} // namespace lyricsmpris::detail
