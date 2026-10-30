#pragma once

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace Log {
namespace detail {

template <class T>
consteval auto pretty() noexcept {
    return std::string_view{__PRETTY_FUNCTION__};
}

inline constexpr std::string_view kProbe  = "double";
inline constexpr std::string_view kSample = pretty<double>();
inline constexpr std::size_t      kHead   = kSample.find(kProbe);
inline constexpr std::size_t      kTail   = kSample.size() - kHead - kProbe.size();
static_assert(kHead != std::string_view::npos, "unsupported compiler: __PRETTY_FUNCTION__ layout");

template <class T>
consteval std::string_view raw() noexcept {
    constexpr auto s = pretty<T>();
    return s.substr(kHead, s.size() - kHead - kTail);
}

struct rule {
    std::string_view from, to;
};

inline constexpr rule kRules[] = {
    {"std::__cxx11::", "std::"}, // libstdc++
    {"std::__1::", "std::"},     // libc++
    {"long long unsigned int", "unsigned long long"},
    {"long long int", "long long"},
    {"long unsigned int", "unsigned long"},
    {"short unsigned int", "unsigned short"},
    {"long int", "long"},
    {"short int", "short"},
    {"std::basic_string<char>", "std::string"},
    {"std::basic_string<wchar_t>", "std::wstring"},
    {"std::basic_string<char8_t>", "std::u8string"},
    {"std::basic_string<char16_t>", "std::u16string"},
    {"std::basic_string<char32_t>", "std::u32string"},
    {"std::basic_string_view<char>", "std::string_view"},
    {"std::basic_string_view<wchar_t>", "std::wstring_view"},
    {"std::basic_string_view<char8_t>", "std::u8string_view"},
    {"std::basic_string_view<char16_t>", "std::u16string_view"},
    {"std::basic_string_view<char32_t>", "std::u32string_view"},
    {"(anonymous namespace)", "{anonymous}"},
    {" &&", "&&"}, {" &", "&"}, {" *", "*"}, {" [", "["}, {" >", ">"}, {"* const", "*const"},
};

consteval bool rules_never_grow() {
    for (const rule& r : kRules)
        if (r.to.size() > r.from.size())
            return false;
    return true;
}

static_assert(rules_never_grow());

constexpr bool is_ident(char c) noexcept {
    return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

template <std::size_t N>
struct buffer {
    char                       data[N + 1]{};
    std::size_t                size{};
    constexpr std::string_view view() const noexcept { return {data, size}; }
};

template <std::size_t N>
constexpr void replace_all(buffer<N>& b, const rule& r) {
    const bool need_left  = is_ident(r.from.front());
    const bool need_right = is_ident(r.from.back());
    buffer<N>  out;
    for (std::size_t i = 0; i < b.size;) {
        const std::size_t end = i + r.from.size();
        const bool        hit = b.view().substr(i).starts_with(r.from)
            && !(need_left && i > 0 && is_ident(b.data[i - 1]))
            && !(need_right && end < b.size && is_ident(b.data[end]));
        if (hit) {
            for (char c : r.to)
                out.data[out.size++] = c;
            i = end;
        }
        else { out.data[out.size++] = b.data[i++]; }
    }
    b = out;
}

template <class T>
consteval auto normalize() {
    constexpr std::string_view src = raw<T>();
    buffer<src.size()>         b;
    for (std::size_t i = 0; i < src.size(); ++i)
        b.data[i] = src[i];
    b.size = src.size();
    for (const rule& r : kRules)
        replace_all(b, r);
    return b;
}

template <class T>
inline constexpr auto storage = normalize<T>();

}

template <class T>
[[nodiscard]] consteval std::string_view type_name() noexcept {
    return detail::storage<T>.view();
}

template <class T>
[[nodiscard]] consteval std::string_view type_name_of(T&&) noexcept {
    return type_name<std::remove_reference_t<T> >();
}

}