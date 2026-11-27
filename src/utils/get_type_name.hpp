#pragma once

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace Log {

namespace Detail {

/// @brief Returns __PRETTY_FUNCTION__ as a string_view.
template<class T>
consteval auto pretty() {
	return std::string_view{__PRETTY_FUNCTION__};
}

inline constexpr std::string_view k_probe{"double"};
inline constexpr std::string_view k_sample{pretty<double>()};
inline constexpr size_t           k_head{k_sample.find(k_probe)};
inline constexpr size_t           k_tail{k_sample.size() - k_head - k_probe.size()};
static_assert(k_head != std::string_view::npos, "unsupported compiler: __PRETTY_FUNCTION__ layout");

/// @brief Extracts the raw type name from __PRETTY_FUNCTION__.
template<class T>
consteval std::string_view raw() {
	constexpr auto k_s{pretty<T>()};
	return k_s.substr(k_head, k_s.size() - k_head - k_tail);
}

/// @brief A single substring rewrite applied to a raw type name.
struct Rule {
	std::string_view from_;
	std::string_view to_;
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

/// @brief Checks that no rewrite rule can increase the buffer size.
consteval bool has_no_growing_rules() {
	for(const Rule& r : k_rules)
		if(r.to_.size() > r.from_.size())
			return false;
	return true;
}

static_assert(has_no_growing_rules());

/// @brief Checks whether c can appear in a C++ identifier.
constexpr bool is_ident(char c) {
	return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/// @brief Fixed-size character buffer used during constexpr normalization.
template<size_t N>
struct Buffer {
	char   data_[N + 1]{};
	size_t size_{};

	/// @brief Returns the current buffer contents.
	[[nodiscard]] constexpr std::string_view view() const { return {data_, size_}; }
};

/// @brief Applies one rewrite rule in place.
template<size_t N>
constexpr void replace_all(Buffer<N>& b, const Rule& r) {
	const bool needs_left{is_ident(r.from_.front())};
	const bool needs_right{is_ident(r.from_.back())};
	Buffer<N>  out;
	for(size_t i = 0; i < b.size_;) {
		const size_t end{i + r.from_.size()};
		const bool   is_hit = b.view().substr(i).starts_with(r.from_) &&
			!(needs_left && i > 0 && is_ident(b.data_[i - 1])) &&
			!(needs_right && end < b.size_ && is_ident(b.data_[end]));

		if(is_hit) {
			for(char c : r.to_)
				out.data_[out.size_++] = c;
			i = end;
		} else {
			out.data_[out.size_++] = b.data_[i++];
		}
	}
	b = out;
}

/// @brief Computes the normalized type name for T.
template<class T>
consteval auto normalize() {
	constexpr std::string_view src{raw<T>()};
	Buffer<src.size()>         b;
	for(size_t i = 0; i < src.size(); ++i)
		b.data_[i] = src[i];
	b.size_ = src.size();
	for(const Rule& r : k_rules)
		replace_all(b, r);
	return b;
}

/// @brief Cached normalized type name for T.
template<class T>
inline constexpr auto storage{normalize<T>()};

}

/// @brief Returns a readable, normalized name for type T at compile time.
template<class T>
[[nodiscard]] consteval std::string_view type_name() {
	return Detail::storage<T>.view();
}

/// @brief Returns a readable, normalized name for the type of value at compile time.
template<class T>
[[nodiscard]] consteval std::string_view type_name_of(T&&) {
	return type_name<std::remove_reference_t<T>>();
}

}
