#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class Color;

// output format
// if your log contains source_location, please only use filename and column.
// template:
// {filename}:{column}: "..."

// if your log contains special symbol that need espace
// Ex. (" \"key\",\"value\" ")
// please use R"_(...)_"
// (R"_("key", "value")_")

// if there's a key & value, write them like this:
// key="...", value="..."
// instead of {key, value}

// if there's a path, write them like this:
// path="..."
// instead of just writing it.

namespace Log {

/// @brief Severity level for log output.
enum class LogLevel : uint8_t {
	Error,
	Warning,
	Debug,
};

}

#if defined(_DEBUG) || !defined(NDEBUG)
inline constexpr bool k_is_debug_mode{true};
#else
inline constexpr bool k_is_debug_mode{};
#endif

namespace Log {

inline constexpr auto k_reset  = "\033[0m";
inline constexpr auto k_green  = "\033[32m";
inline constexpr auto k_red    = "\033[31m";
inline constexpr auto k_gray   = "\033[90m";
inline constexpr auto k_yellow = "\033[33m";

/// @brief Writes a formatted message at the given level to stderr/stdout.
inline void logger(LogLevel level, std::string_view msg) {
	if(level == LogLevel::Error) {
		std::println(stderr, "{}[ERROR]{} {}", k_red, k_reset, msg);
	} else if(level == LogLevel::Debug) {
		if constexpr(k_is_debug_mode)
			std::println("{}[DEBUG]{} {}", k_gray, k_reset, msg);
	} else if(level == LogLevel::Warning) {
		std::println(stderr, "{}[WARNING]{} {}", k_yellow, k_reset, msg);
	}
}

/// @brief Logs an error message and exits the process with failure.
[[noreturn]] inline void fatal(std::string_view msg) {
	logger(LogLevel::Error, msg);
	std::exit(EXIT_FAILURE);
}

/// @brief Logs a formatted error message and exits the process with failure.
template<typename... Args>
[[noreturn]] void fatal(std::format_string<Args...> fmt, Args&&... args) {
	fatal(std::format(fmt, std::forward<Args>(args)...));
}

/// @brief Writes a formatted message at the given level.
template<typename... Args>
inline void logger(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
	logger(level, std::format(fmt, std::forward<Args>(args)...));
}

/// @brief Prints newline-separated messages inside a boxed frame.
template<typename... Args>
inline void frame_logger(LogLevel level, Args&&... args) {
	if(!k_is_debug_mode && (level == LogLevel::Debug))
		return;
	if constexpr(sizeof...(Args) == 0)
		return;

	const std::array<std::string_view, sizeof...(Args)> raw_msgs{std::string_view{std::forward<Args>(args)}...};

	std::vector<std::string_view> msgs{};
	for(auto s : raw_msgs) {
		do {
			const auto pos{s.find('\n')};
			msgs.push_back(s.substr(0, pos));
			if(pos == std::string_view::npos)
				break;
			s.remove_prefix(pos + 1);
		} while(!s.empty());
	}

	size_t msg_len{};
	size_t total_lines{};
	for(const auto& s : msgs) {
		msg_len = std::max(s.size(), msg_len);
		total_lines += s.empty() ? 1 : (s.size() + 79) / 80;
	}
	msg_len = std::min<size_t>(msg_len, 80);

	const size_t border_bytes{3 + (msg_len + 2) * 3 + 4};
	const size_t content_bytes{total_lines * (4 + msg_len + 5)};

	std::string out_msg{};
	out_msg.reserve(border_bytes * 2 + content_bytes);

	out_msg += "┌";
	for(size_t i{}; i < msg_len + 2; ++i)
		out_msg += "─";
	out_msg += "┐\n";

	for(const std::string_view s : msgs) {
		size_t handled_char{};
		while(s.size() - handled_char > 80) {
			std::string_view line{s.substr(handled_char, 80)};
			out_msg += std::vformat("│ {:<{}} │\n", std::make_format_args(line, msg_len));
			handled_char += 80;
		}
		std::string_view line{s.substr(handled_char)};
		out_msg += std::vformat("│ {:<{}} │\n", std::make_format_args(line, msg_len));
	}

	out_msg += "└";
	for(size_t i{}; i < msg_len + 2; ++i)
		out_msg += "─";
	out_msg += "┘\n";

	std::print("{}", out_msg);
}

}
