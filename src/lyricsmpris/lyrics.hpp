#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LyricsMpris {

/// @brief Millisecond duration used for lyric timestamps and positions.
using Milliseconds = std::chrono::milliseconds;

/// @brief One lyric line with an optional timestamp.
struct LyricLine {
	// Missing for unsynchronized plain text. Empty text is a timed blank line.
	std::optional<Milliseconds> start_time_{};
	std::string_view            text_{};
};

/// @brief Reason a lyrics document could not be parsed.
enum class ParseError : uint8_t {
	TooLarge,
	TooManyLines,
	InvalidUtf8,
};

/// @brief Resource limits applied while parsing a lyrics document.
struct ParseLimits {
	size_t max_bytes_{1024 * 1024};
	size_t max_lines_{10000};
};

/// @brief Parsed lyrics document backed by one text buffer and compact line indexes.
///
/// Offsets keep moves/copies safe, including short strings. Returned line views
/// borrow this document's storage.
class Lyrics {
public:
	/// @brief Returns the number of lyric lines.
	[[nodiscard]] size_t size() const { return lines_.size(); }

	/// @brief Returns true when the document has no lines and is not instrumental.
	[[nodiscard]] bool is_empty() const { return lines_.empty() && !is_instrumental_; }

	/// @brief Returns true when the lines carry timestamps.
	[[nodiscard]] bool is_synced() const { return is_synced_; }

	/// @brief Returns true when the document is marked instrumental.
	[[nodiscard]] bool is_instrumental() const { return is_instrumental_; }

	/// @brief Returns the line at index; throws when out of range.
	[[nodiscard]] LyricLine line(size_t index) const;
	/// @brief Returns the index of the line active at position.
	[[nodiscard]] std::optional<size_t> line_at(Milliseconds position) const;

	/// @brief Returns the provider that supplied the document.
	[[nodiscard]] std::string_view get_provider() const { return provider_; }

	/// @brief Returns the approximate number of bytes owned by the document.
	[[nodiscard]] size_t get_storage_bytes() const;

private:
	struct Entry {
		int64_t  time_ms_{};
		uint32_t offset_{};
		uint32_t length_{};
	};

	std::string        text_{};
	std::vector<Entry> lines_{};
	std::string        provider_{};
	bool               is_synced_{};
	bool               is_instrumental_{};

	friend std::expected<Lyrics, ParseError> parse_lyrics(std::string_view, std::string_view, ParseLimits);
	friend class DetailLyricsAccess;
};

/// @brief Parses an LRC or plain-text lyrics document.
[[nodiscard]] std::expected<Lyrics, ParseError>
parse_lyrics(std::string_view text, std::string_view provider = {}, ParseLimits limits = {});

}
