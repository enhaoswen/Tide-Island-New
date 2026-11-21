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

inline constexpr std::string_view k_probe = "double";
inline constexpr std::string_view k_sample = pretty<double>();
inline constexpr std::size_t k_head = k_sample.find(k_probe);
inline constexpr std::size_t k_tail = k_sample.size() - k_head - k_probe.size();
static_assert(k_head != std::string_view::npos, "unsupported compiler: __PRETTY_FUNCTION__ layout");

template <class T>
consteval std::string_view raw() noexcept {
    constexpr auto s = pretty<T>();
    return s.substr(k_head, s.size() - k_head - k_tail);
}

struct Rule {
    std::string_view from, to;
};

inline constexpr Rule k_rules[] = {
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
    {" &&", "&&"},
    {" &", "&"},
    {" *", "*"},
    {" [", "["},
    {" >", ">"},
    {"* const", "*const"},
};

consteval bool rules_never_grow() {
    for (const Rule& r : k_rules)
        if (r.to.size() > r.from.size())
            return false;
    return true;
}

static_assert(rules_never_grow());

constexpr bool is_ident(char c) noexcept {
    return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

template <std::size_t N>
struct Buffer {
    char data[N + 1]{};
    std::size_t size{};
    [[nodiscard]] constexpr std::string_view view() const noexcept { return {data, size}; }
};

template <std::size_t N>
constexpr void replace_all(Buffer<N>& b, const Rule& r) {
    const bool need_left = is_ident(r.from.front());
    const bool need_right = is_ident(r.from.back());
    Buffer<N> out;
    for (std::size_t i = 0; i < b.size;) {
        const std::size_t end = i + r.from.size();
        const bool hit =
            b.view().substr(i).starts_with(r.from) &&
            !(need_left && i > 0 && is_ident(b.data[i - 1])) &&
            !(need_right && end < b.size && is_ident(b.data[end]));

        if (hit) {
            for (char c : r.to)
                out.data[out.size++] = c;
            i = end;
        }
        else {
            out.data[out.size++] = b.data[i++];
        }
    }
    b = out;
}

template <class T>
consteval auto normalize() {
    constexpr std::string_view src = raw<T>();
    Buffer<src.size()> b;
    for (std::size_t i = 0; i < src.size(); ++i)
        b.data[i] = src[i];
    b.size = src.size();
    for (const Rule& r : k_rules)
        replace_all(b, r);
    return b;
}

template <class T>
inline constexpr auto storage = normalize<T>();

} // namespace detail

template <class T>
[[nodiscard]] consteval std::string_view type_name() noexcept {
    return detail::storage<T>.view();
}

template <class T>
[[nodiscard]] consteval std::string_view type_name_of(T&&) noexcept {
    return type_name<std::remove_reference_t<T>>();
}

} // namespace Log
