#include "Normalize.hpp"
#include "Utility.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <utf8proc.h>

using namespace std;

namespace lyricsmpris::detail {
namespace {

struct Pair { utf8proc_int32_t traditional, simplified; };
constexpr Pair pairs[] = {
#include "CjkPairs.inc"
};

bool cjk(utf8proc_int32_t code) noexcept {
    return (code >= 0x3400 && code <= 0x9fff) || (code >= 0xf900 && code <= 0xfaff)
        || (code >= 0x20000 && code <= 0x323af);
}

void append(string& output, utf8proc_int32_t code) {
    utf8proc_uint8_t buffer[4];
    const auto count = utf8proc_encode_char(code, buffer);
    if (count > 0) output.append(reinterpret_cast<char*>(buffer), static_cast<size_t>(count));
}

bool space(utf8proc_int32_t code) noexcept {
    const auto category = utf8proc_category(code);
    return (code >= 9 && code <= 13) || code == 32 || category == UTF8PROC_CATEGORY_ZS
        || category == UTF8PROC_CATEGORY_ZL || category == UTF8PROC_CATEGORY_ZP;
}

string fold(string_view value) {
    utf8proc_uint8_t* mapped = nullptr;
    const auto count = utf8proc_map(reinterpret_cast<const utf8proc_uint8_t*>(value.data()),
        static_cast<utf8proc_ssize_t>(value.size()), &mapped,
        static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPAT | UTF8PROC_COMPOSE | UTF8PROC_CASEFOLD));
    unique_ptr<utf8proc_uint8_t, decltype(&free)> owner(mapped, &free);
    if (count < 0) return {};
    string result;
    result.reserve(static_cast<size_t>(count));
    for (utf8proc_ssize_t offset = 0; offset < count;) {
        utf8proc_int32_t code{};
        const auto used = utf8proc_iterate(mapped + offset, count - offset, &code);
        if (used <= 0) break;
        offset += used;
        const auto pair = lower_bound(begin(pairs), end(pairs), code,
            [](const Pair& pair, auto key) { return pair.traditional < key; });
        append(result, pair != end(pairs) && pair->traditional == code ? pair->simplified : code);
    }
    return result;
}

bool word_at(string_view value, size_t at, size_t length) {
    auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    return (at == 0 || !letter(value[at - 1]))
        && (at + length == value.size() || !letter(value[at + length]));
}

bool word(string_view value, string_view term) {
    for (auto at = value.find(term); at != value.npos; at = value.find(term, at + 1))
        if (word_at(value, at, term.size())) return true;
    return false;
}

bool noise(string_view value) {
    for (auto term : {"feat", "ft", "featuring", "with", "remaster", "remastered", "live",
                      "mono", "stereo", "explicit", "clean", "radio", "edit", "version",
                      "official", "audio", "video", "lyric", "lyrics", "mv", "hd", "cover", "remix"})
        if (word(value, term)) return true;
    for (auto term : {"伴奏", "钢琴", "纯音乐", "翻唱", "原唱", "加速", "降速", "混音", "现场", "演唱会", "版"})
        if (value.find(term) != value.npos) return true;
    return false;
}

} // namespace

bool valid_utf8(string_view value) noexcept {
    while (!value.empty()) {
        utf8proc_int32_t code{};
        const auto used = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(value.data()),
            static_cast<utf8proc_ssize_t>(value.size()), &code);
        if (used <= 0 || code == 0) return false;
        value.remove_prefix(static_cast<size_t>(used));
    }
    return true;
}

string clean_text(string_view text) {
    string result;
    result.reserve(text.size());
    bool pending_space = false;
    while (!text.empty()) {
        if (text.front() == '<') {
            const auto end = text.find('>');
            if (end != text.npos && end < 256 && text.size() > 1
                && (text[1] == '/' || (text[1] >= 'a' && text[1] <= 'z')
                    || (text[1] >= 'A' && text[1] <= 'Z') || (text[1] >= '0' && text[1] <= '9'))) {
                if (text.substr(0, end).starts_with("<br")) pending_space = !result.empty();
                text.remove_prefix(end + 1);
                continue;
            }
        }
        if (text.front() == '[') {
            const auto end = text.find(']');
            const auto contents = text.substr(1, end == text.npos ? 0 : end - 1);
            if (end != text.npos && !contents.empty() && contents.find(',') != contents.npos
                && contents.find_first_not_of("0123456789,") == contents.npos) {
                text.remove_prefix(end + 1);
                continue;
            }
        }
        utf8proc_int32_t code{};
        size_t count = 0;
        if (text.front() == '&') {
            const auto end = text.find(';');
            if (end != text.npos && end <= 12) {
                const auto entity = text.substr(1, end - 1);
                if (entity == "nbsp") code = ' ';
                else if (entity == "amp") code = '&';
                else if (entity == "quot") code = '"';
                else if (entity == "apos") code = '\'';
                else if (entity == "lt") code = '<';
                else if (entity == "gt") code = '>';
                else if (entity.starts_with('#')) {
                    auto digits = entity.substr(1);
                    int base = 10;
                    if (digits.starts_with('x') || digits.starts_with('X')) { base = 16; digits.remove_prefix(1); }
                    const auto [last, error] = from_chars(digits.data(), digits.data() + digits.size(), code, base);
                    if (error != errc{} || last != digits.data() + digits.size() || code <= 0
                        || !utf8proc_codepoint_valid(code)) code = 0;
                }
                if (code != 0) count = end + 1;
            }
        }
        if (count == 0) {
            const auto used = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(text.data()),
                static_cast<utf8proc_ssize_t>(text.size()), &code);
            if (used <= 0) { text.remove_prefix(1); continue; }
            count = static_cast<size_t>(used);
        }
        text.remove_prefix(count);
        if (space(code)) { pending_space = !result.empty(); continue; }
        if (pending_space) { result += ' '; pending_space = false; }
        append(result, code);
    }
    return result;
}

string normalize(string_view text, bool title) {
    string value = fold(clean_text(text));
    // NFKC converts full-width brackets, but not the CJK lenticular brackets.
    for (const auto& [open, close] : {pair{"(", ")"}, {"[", "]"}, {"{", "}"}, {"【", "】"}}) {
        size_t at = 0;
        while ((at = value.find(open, at)) != value.npos) {
            const auto end = value.find(close, at + string_view(open).size());
            if (end == value.npos) break;
            const auto size = end + string_view(close).size() - at;
            if (noise(string_view(value).substr(at, size))) value.erase(at, size);
            else at = end + string_view(close).size();
        }
    }
    if (title) {
        const auto separator = value.find(" - ");
        if (separator != value.npos && noise(string_view(value).substr(separator + 3))) value.resize(separator);
        const auto digits = value.find_first_not_of("0123456789");
        if (digits > 0 && digits <= 3 && digits < value.size() && string_view(".-_)").find(value[digits]) != string_view::npos)
            value.erase(0, digits + 1);
    }
    for (auto term : {"feat", "ft", "featuring", "with"}) {
        for (auto at = value.find(term); at != value.npos; at = value.find(term, at + 1)) {
            if (word_at(value, at, string_view(term).size()) && (at == 0 || value[at - 1] == ' ')) {
                value.resize(at); break;
            }
        }
    }
    string result;
    result.reserve(value.size());
    bool separator = false;
    string_view remaining = value;
    while (!remaining.empty()) {
        utf8proc_int32_t code{};
        const auto used = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(remaining.data()),
            static_cast<utf8proc_ssize_t>(remaining.size()), &code);
        if (used <= 0) break;
        remaining.remove_prefix(static_cast<size_t>(used));
        const auto category = utf8proc_category(code);
        if (category >= UTF8PROC_CATEGORY_LU && category <= UTF8PROC_CATEGORY_NO) {
            if (separator && !result.empty()) result += ' ';
            separator = false;
            append(result, code);
        } else separator = true;
    }
    return result;
}

vector<string> tokens(string_view value) {
    vector<string> result;
    string run;
    auto flush = [&] {
        if (!run.empty() && run != "the" && run != "and" && run != "a" && run != "an") result.push_back(move(run));
        run.clear();
    };
    while (!value.empty()) {
        utf8proc_int32_t code{};
        const auto used = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(value.data()),
            static_cast<utf8proc_ssize_t>(value.size()), &code);
        if (used <= 0) break;
        const auto bytes = value.substr(0, static_cast<size_t>(used));
        value.remove_prefix(static_cast<size_t>(used));
        if (cjk(code)) { flush(); result.emplace_back(bytes); }
        else if (code == ' ') flush();
        else run.append(bytes);
    }
    flush();
    sort(result.begin(), result.end());
    result.erase(unique(result.begin(), result.end()), result.end());
    return result;
}

double similarity(string_view left, string_view right, bool title) {
    const auto a = normalize(left, title), b = normalize(right, title);
    if (a.empty() || b.empty()) return 0;
    if (a == b) return 1;
    const auto aa = tokens(a), bb = tokens(b);
    if (aa.empty() || bb.empty()) return 0;
    size_t common = 0;
    for (const auto& token : aa) common += binary_search(bb.begin(), bb.end(), token);
    return 2.0 * static_cast<double>(common) / static_cast<double>(aa.size() + bb.size());
}

bool contains_tokens(string_view needle, string_view haystack) {
    const auto a = tokens(normalize(needle)), b = tokens(normalize(haystack));
    return !a.empty() && includes(b.begin(), b.end(), a.begin(), a.end());
}

bool comparable_script(string_view left, string_view right) {
    auto scripts = [](string value) {
        unsigned flags = 0;
        string_view view = value;
        while (!view.empty()) {
            utf8proc_int32_t code{};
            const auto used = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(view.data()),
                static_cast<utf8proc_ssize_t>(view.size()), &code);
            if (used <= 0) break;
            view.remove_prefix(static_cast<size_t>(used));
            if (cjk(code)) flags |= 1;
            if ((code >= 'a' && code <= 'z') || (code >= 0xc0 && code <= 0x24f)) flags |= 2;
        }
        return flags;
    };
    return (scripts(fold(left)) & scripts(fold(right))) != 0;
}

unsigned version_flags(string_view value) {
    const auto text = fold(value);
    auto any = [&](initializer_list<string_view> terms) {
        return any_of(terms.begin(), terms.end(), [&](auto term) { return text.find(term) != text.npos; });
    };
    unsigned flags = 0;
    if (word(text, "live") || any({"演唱会", "现场", "巡回", "concert"})) flags |= 1;
    if (word(text, "cover") || any({"翻唱", "原唱"})) flags |= 2;
    if (any({"instrumental", "伴奏", "纯音乐", "钢琴", "piano", "karaoke"})) flags |= 4;
    if (word(text, "remix") || any({"dj版", "dj 版", "混音"})) flags |= 8;
    if (any({"加速", "降速", "变速", "sped up", "slowed"})) flags |= 16;
    return flags;
}

} // namespace lyricsmpris::detail
