#pragma once

#include <algorithm>
#include <array>
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
// please use R"@(...)@"
// (R"@("key", "value")@")

// if there's a key & value, write them like this:
// key="...", value="..."
// instead of {key, value}

// if there's a path, write them like this:
// path="..."
// instead of just writing it.

namespace Log {

enum LogLevel : char {
    Error,
    Warning,
    Debug
};

} // namespace Log

// ============================================================================
// [Internal Details]
// ============================================================================

namespace {

inline constexpr auto RESET = "\033[0m";
inline constexpr auto GREEN = "\033[32m";
inline constexpr auto RED = "\033[31m";
inline constexpr auto GRAY = "\033[90m";
inline constexpr auto YELLOW = "\033[33m";

#if defined(_DEBUG) || !defined(NDEBUG)
inline constexpr bool is_debug_mode = true;
#else
inline constexpr bool is_debug_mode = false;
#endif

} // namespace

// ============================================================================
// [Public API Implementation]
// ============================================================================


namespace Log {

inline void logger(LogLevel level, std::string_view msg) {
    if (level == LogLevel::Error) {
        std::println(stderr, "{}[ERROR]{} {}", RED, RESET, msg);
    }
    else if (level == LogLevel::Debug) {
        if constexpr (is_debug_mode)
            std::println("{}[DEBUG]{} {}", GRAY, RESET, msg);
    }
    else if (level == LogLevel::Warning) {
        std::println(stderr, "{}[WARNING]{} {}", YELLOW, RESET, msg);
    }
}

inline void fatal(std::string_view msg) {
    logger(LogLevel::Error, msg);
    std::exit(EXIT_FAILURE);
}

template <typename... Args>
void fatal(std::format_string<Args...> fmt, Args&&... args) {
    fatal(std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
inline void logger(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
    logger(level, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
inline void frame_logger(LogLevel level, Args&&... args) {
    if (!is_debug_mode && (level == LogLevel::Debug))
        return;
    if constexpr (sizeof...(Args) == 0)
        return;

    std::array<std::string_view, sizeof...(Args)> msgs{
        std::string_view{std::forward<Args>(args)}...};

    size_t msg_len = 0;
    size_t total_lines = 0;
    for (const auto& s : msgs) {
        if (s.size() > msg_len)
            msg_len = s.size();
        total_lines += s.empty() ? 1 : (s.size() + 79) / 80;
    }
    msg_len = std::min<size_t>(msg_len, 80);

    size_t border_bytes = 3 + (msg_len + 2) * 3 + 4;
    size_t content_bytes = total_lines * (4 + msg_len + 5);

    std::string out_msg;
    out_msg.reserve(border_bytes * 2 + content_bytes);

    out_msg += "┌";
    for (size_t i = 0; i < msg_len + 2; ++i)
        out_msg += "─";
    out_msg += "┐\n";

    for (std::string_view s : msgs) {
        size_t handled_char = 0;
        while (s.size() - handled_char > 80) {
            std::string_view line = s.substr(handled_char, 80);
            out_msg += std::vformat("│ {:<{}} │\n", std::make_format_args(line, msg_len));
            handled_char += 80;
        }
        std::string_view line = s.substr(handled_char);
        out_msg += std::vformat("│ {:<{}} │\n", std::make_format_args(line, msg_len));
    }

    out_msg += "└";
    for (size_t i = 0; i < msg_len + 2; ++i)
        out_msg += "─";
    out_msg += "┘\n";

    print("{}", out_msg);
}

// make sure your type doesn't contain any special characters, 
// or you have to escape them yourself.
template <typename T>
std::string get_type_name() {
    if constexpr (std::is_same_v<T, int>) {
        return "int";
    }
    else if constexpr (std::is_same_v<T, float>) {
        return "float";
    }
    else if constexpr (std::is_same_v<T, std::string>) {
        return "string";
    }
    else if constexpr (std::is_same_v<T, bool>) {
        return "bool";
    }
    else if constexpr (std::is_same_v<T, std::vector<int>>) {
        return "list<int>";
    }
    else if constexpr (std::is_same_v<T, std::vector<float>>) {
        return "list<float>";
    }
    else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
        return "list<string>";
    }
    else if constexpr (std::is_same_v<T, std::vector<bool>>) {
        return "list<bool>";
    }
    else if constexpr (std::is_same_v<T, Color>) {
        return "Color";
    }
    else {
        return typeid(T).name();
    }
}

} // namespace Log
