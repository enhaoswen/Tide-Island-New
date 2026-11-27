#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace LyricsMpris::Detail {

/// @brief Normalizes text for comparison.
std::string normalize(std::string_view text, bool is_title = false);
/// @brief Removes markup and timestamps from lyric text.
std::string clean_text(std::string_view text);
/// @brief Splits normalized text into sorted unique tokens.
std::vector<std::string> tokens(std::string_view normalized);
/// @brief Returns a similarity score for two normalized strings.
double similarity(std::string_view left, std::string_view right, bool is_title);
/// @brief Returns true when all needle tokens appear in the haystack.
bool has_tokens(std::string_view needle, std::string_view haystack);
/// @brief Returns true when the strings share a comparable script.
bool has_comparable_script(std::string_view left, std::string_view right);
/// @brief Returns bit flags describing alternate-version markers.
unsigned version_flags(std::string_view value);
/// @brief Returns true when the value is valid UTF-8.
bool is_valid_utf8(std::string_view value);

}
