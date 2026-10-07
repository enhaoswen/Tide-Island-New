#include "Lyrics.hpp"
#include "detail/Normalize.hpp"
#include "detail/Utility.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

using namespace std;

namespace lyricsmpris {
namespace {

optional<int64_t> timestamp(string_view tag) {
    const auto colon = tag.find(':');
    if (colon == tag.npos || colon == 0 || colon > 6) return {};
    const auto minutes = detail::number<int64_t>(tag.substr(0, colon));
    auto rest = tag.substr(colon + 1);
    const auto dot = rest.find_first_of(".:");
    const auto seconds = detail::number<unsigned>(rest.substr(0, dot));
    if (!minutes || *minutes < 0 || !seconds || *seconds >= 60) return {};
    int64_t fraction = 0;
    if (dot != rest.npos) {
        const auto digits = rest.substr(dot + 1);
        if (digits.empty() || digits.size() > 3 || digits.find_first_not_of("0123456789") != digits.npos) return {};
        fraction = *detail::number<unsigned>(digits);
        if (digits.size() == 1) fraction *= 100;
        if (digits.size() == 2) fraction *= 10;
    }
    return *minutes * 60000 + *seconds * 1000 + fraction;
}

bool placeholder(string_view text) {
    const auto value = detail::normalize(text);
    return value == "暂无歌词" || value == "暂无" || value == "纯音乐"
        || value == "纯音乐 请欣赏" || value == "instrumental" || value == "没有填词";
}

} // namespace

LyricLine Lyrics::line(size_t index) const {
    const auto& entry = lines_.at(index);
    return {synced_ ? optional{Milliseconds{entry.time_ms}} : nullopt,
        string_view(text_).substr(entry.offset, entry.length)};
}

optional<size_t> Lyrics::line_at(Milliseconds position) const noexcept {
    if (lines_.empty()) return {};
    if (!synced_) return 0;
    const auto after = upper_bound(lines_.begin(), lines_.end(), position.count(),
        [](auto time, const Entry& entry) { return time < entry.time_ms; });
    if (after == lines_.begin()) return {};
    return static_cast<size_t>(after - lines_.begin() - 1);
}

size_t Lyrics::storage_bytes() const noexcept {
    return sizeof(*this) + text_.capacity() + lines_.capacity() * sizeof(Entry) + provider_.capacity();
}

expected<Lyrics, ParseError> parse_lyrics(string_view text, string_view provider, ParseLimits limits) {
    if (text.size() > limits.max_bytes || text.size() > numeric_limits<uint32_t>::max())
        return unexpected(ParseError::TooLarge);
    if (!detail::valid_utf8(text)) return unexpected(ParseError::InvalidUtf8);
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    Lyrics result;
    result.provider_ = provider;
    result.text_.reserve(text.size());
    result.lines_.reserve(min({limits.max_lines, text.size() / 24 + 1, size_t{512}}));
    vector<Lyrics::Entry> plain;
    vector<int64_t> times;
    int64_t offset_ms = 0;
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
                if (times.size() >= limits.max_lines) return unexpected(ParseError::TooManyLines);
                times.push_back(*time);
                row.remove_prefix(close + 1);
                row = detail::trim(row);
            } else if (!tag.empty() && ((tag.front() >= 'a' && tag.front() <= 'z')
                || (tag.front() >= 'A' && tag.front() <= 'Z')) && tag.find(':') != tag.npos) {
                if (tag.starts_with("offset:")) {
                    if (auto offset = detail::number<int64_t>(tag.substr(7))) offset_ms = *offset;
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
        const auto offset = static_cast<uint32_t>(result.text_.size());
        result.text_.append(cleaned);
        if (times.empty()) {
            if (plain.size() >= limits.max_lines) return unexpected(ParseError::TooManyLines);
            plain.push_back({0, offset, static_cast<uint32_t>(cleaned.size())});
        } else {
            if (times.size() > limits.max_lines - result.lines_.size()) return unexpected(ParseError::TooManyLines);
            for (const auto time : times) result.lines_.push_back({time, offset, static_cast<uint32_t>(cleaned.size())});
        }
    }
    result.synced_ = !result.lines_.empty();
    if (!result.synced_) result.lines_ = move(plain);
    else {
        for (auto& entry : result.lines_) {
            if (offset_ms > 0 && entry.time_ms > numeric_limits<int64_t>::max() - offset_ms)
                entry.time_ms = numeric_limits<int64_t>::max();
            else entry.time_ms = max<int64_t>(0, entry.time_ms + offset_ms);
        }
        stable_sort(result.lines_.begin(), result.lines_.end(),
            [](const auto& a, const auto& b) { return a.time_ms < b.time_ms; });
    }
    // A successful text document takes precedence over placeholder markers.
    if (!result.lines_.empty()) result.instrumental_ = false;
    result.text_.shrink_to_fit();
    result.lines_.shrink_to_fit();
    return result;
}

} // namespace lyricsmpris
