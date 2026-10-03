#include "Lyrics.hpp"
#include "detail/Normalize.hpp"
#include "detail/Utility.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace lyricsmpris {
namespace {

std::optional<std::int64_t> timestamp(std::string_view tag) {
    const auto colon = tag.find(':');
    if (colon == tag.npos || colon == 0 || colon > 6) return {};
    const auto minutes = detail::number<std::int64_t>(tag.substr(0, colon));
    auto rest = tag.substr(colon + 1);
    const auto dot = rest.find_first_of(".:");
    const auto seconds = detail::number<unsigned>(rest.substr(0, dot));
    if (!minutes || *minutes < 0 || !seconds || *seconds >= 60) return {};
    std::int64_t fraction = 0;
    if (dot != rest.npos) {
        const auto digits = rest.substr(dot + 1);
        if (digits.empty() || digits.size() > 3 || digits.find_first_not_of("0123456789") != digits.npos) return {};
        fraction = *detail::number<unsigned>(digits);
        if (digits.size() == 1) fraction *= 100;
        if (digits.size() == 2) fraction *= 10;
    }
    return *minutes * 60000 + *seconds * 1000 + fraction;
}

bool placeholder(std::string_view text) {
    const auto value = detail::normalize(text);
    return value == "暂无歌词" || value == "暂无" || value == "纯音乐"
        || value == "纯音乐 请欣赏" || value == "instrumental" || value == "没有填词";
}

} // namespace

LyricLine Lyrics::line(std::size_t index) const {
    const auto& entry = lines_.at(index);
    return {synced_ ? std::optional{Milliseconds{entry.time_ms}} : std::nullopt,
        std::string_view(text_).substr(entry.offset, entry.length)};
}

std::optional<std::size_t> Lyrics::line_at(Milliseconds position) const noexcept {
    if (lines_.empty()) return {};
    if (!synced_) return 0;
    const auto after = std::upper_bound(lines_.begin(), lines_.end(), position.count(),
        [](auto time, const Entry& entry) { return time < entry.time_ms; });
    if (after == lines_.begin()) return {};
    return static_cast<std::size_t>(after - lines_.begin() - 1);
}

std::size_t Lyrics::storage_bytes() const noexcept {
    return sizeof(*this) + text_.capacity() + lines_.capacity() * sizeof(Entry) + provider_.capacity();
}

std::expected<Lyrics, ParseError> parse_lyrics(std::string_view text, std::string_view provider, ParseLimits limits) {
    if (text.size() > limits.max_bytes || text.size() > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected(ParseError::TooLarge);
    if (!detail::valid_utf8(text)) return std::unexpected(ParseError::InvalidUtf8);
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    Lyrics result;
    result.provider_ = provider;
    result.text_.reserve(text.size());
    result.lines_.reserve(std::min({limits.max_lines, text.size() / 24 + 1, std::size_t{512}}));
    std::vector<Lyrics::Entry> plain;
    std::vector<std::int64_t> times;
    std::int64_t offset_ms = 0;
    while (!text.empty()) {
        const auto end = text.find_first_of("\r\n");
        auto row = detail::trim(text.substr(0, end));
        if (end == text.npos) text = {};
        else { text.remove_prefix(end + 1); if (!text.empty() && text.front() == '\n') text.remove_prefix(1); }
        if (row.empty()) continue;
        times.clear();
        bool metadata = false;
        while (row.starts_with('[')) {
            const auto close = row.find(']');
            if (close == row.npos) break;
            const auto tag = row.substr(1, close - 1);
            if (const auto time = timestamp(tag)) {
                if (times.size() >= limits.max_lines) return std::unexpected(ParseError::TooManyLines);
                times.push_back(*time);
                row.remove_prefix(close + 1);
                row = detail::trim(row);
            } else if (!tag.empty() && ((tag.front() >= 'a' && tag.front() <= 'z')
                || (tag.front() >= 'A' && tag.front() <= 'Z')) && tag.find(':') != tag.npos) {
                if (tag.starts_with("offset:")) {
                    if (auto offset = detail::number<std::int64_t>(tag.substr(7))) offset_ms = *offset;
                }
                metadata = true;
                break;
            } else break;
        }
        if (metadata) continue;
        const auto cleaned = detail::clean_text(row);
        if (placeholder(cleaned)) {
            const auto normalized = detail::normalize(cleaned);
            if (normalized.starts_with("纯音乐") || normalized == "instrumental") result.instrumental_ = true;
            continue;
        }
        if (times.empty() && cleaned.empty()) continue;
        const auto offset = static_cast<std::uint32_t>(result.text_.size());
        result.text_.append(cleaned);
        if (times.empty()) {
            if (plain.size() >= limits.max_lines) return std::unexpected(ParseError::TooManyLines);
            plain.push_back({0, offset, static_cast<std::uint32_t>(cleaned.size())});
        } else {
            if (times.size() > limits.max_lines - result.lines_.size()) return std::unexpected(ParseError::TooManyLines);
            for (const auto time : times) result.lines_.push_back({time, offset, static_cast<std::uint32_t>(cleaned.size())});
        }
    }
    result.synced_ = !result.lines_.empty();
    if (!result.synced_) result.lines_ = std::move(plain);
    else {
        for (auto& entry : result.lines_) {
            if (offset_ms > 0 && entry.time_ms > std::numeric_limits<std::int64_t>::max() - offset_ms)
                entry.time_ms = std::numeric_limits<std::int64_t>::max();
            else entry.time_ms = std::max<std::int64_t>(0, entry.time_ms + offset_ms);
        }
        std::stable_sort(result.lines_.begin(), result.lines_.end(),
            [](const auto& a, const auto& b) { return a.time_ms < b.time_ms; });
    }
    // A successful text document takes precedence over placeholder markers.
    if (!result.lines_.empty()) result.instrumental_ = false;
    result.text_.shrink_to_fit();
    result.lines_.shrink_to_fit();
    return result;
}

} // namespace lyricsmpris
