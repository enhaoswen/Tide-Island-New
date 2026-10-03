#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lyricsmpris {

using Milliseconds = std::chrono::milliseconds;

struct LyricLine {
    // Missing for unsynchronized plain text. Empty text is a timed blank line.
    std::optional<Milliseconds> start_time;
    std::string_view text;
};

enum class ParseError { TooLarge, TooManyLines, InvalidUtf8 };

struct ParseLimits {
    std::size_t max_bytes = 1024 * 1024;
    std::size_t max_lines = 10000;
};

// One text buffer and compact line indexes. Offsets keep moves/copies safe,
// including short strings. Returned line views borrow this document's storage.
class Lyrics {
public:
    [[nodiscard]] std::size_t size() const noexcept { return lines_.size(); }
    [[nodiscard]] bool empty() const noexcept { return lines_.empty() && !instrumental_; }
    [[nodiscard]] bool synced() const noexcept { return synced_; }
    [[nodiscard]] bool instrumental() const noexcept { return instrumental_; }
    [[nodiscard]] LyricLine line(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> line_at(Milliseconds position) const noexcept;
    [[nodiscard]] std::string_view provider() const noexcept { return provider_; }
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

private:
    struct Entry {
        std::int64_t time_ms;
        std::uint32_t offset;
        std::uint32_t length;
    };
    std::string text_;
    std::vector<Entry> lines_;
    std::string provider_;
    bool synced_ = false;
    bool instrumental_ = false;

    friend std::expected<Lyrics, ParseError> parse_lyrics(
        std::string_view, std::string_view, ParseLimits);
    friend class detail_lyrics_access;
};

[[nodiscard]] std::expected<Lyrics, ParseError> parse_lyrics(
    std::string_view text,
    std::string_view provider = {},
    ParseLimits limits = {}
);

} // namespace lyricsmpris
