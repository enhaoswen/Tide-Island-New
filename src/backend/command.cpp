#include "command.hpp"
#include "app/island.hpp"
#include "utils/log.hpp"

#include <vector>
#include <optional>
#include <ranges>
#include <span>

using namespace std;

namespace {

vector<string_view> split(string_view str, char delimiter = ' ') {
    vector<string_view> result{};

    size_t start{};

    while (start < str.size()) {
        start = str.find_first_not_of(delimiter, start);
        if (start == string_view::npos)
            break;

        const size_t end{str.find(delimiter, start)};

        if (end == string_view::npos) {
            result.push_back(str.substr(start));
            break;
        }

        result.push_back(str.substr(start, end - start));
        start = end + 1;
    }

    return result;
}

optional<float> to_float(string_view str) {
    float value{};

    const auto [ptr, ec] = from_chars(
        str.data(),
        str.data() + str.size(),
        value
        );

    if (ec != errc{} || ptr != str.data() + str.size()) {
        return nullopt;
    }

    return value;
}

void handle_animation(span<const string_view> args) {
    if (args[1] == "speed") {
        const auto value = to_float(args[2]);

        if (!value) {

            // c++ is a amazing language
            const auto cmd = args
                | views::join_with(' ')
                | ranges::to<string>();

            logger(Log::Error, "Error speed value {} in {}, should be float", args[2], cmd);
            return;
        }

        Island::set_animation_scale(value.value());
        Log::logger(Log::Debug, "Animation scale {}", value.value());

    }

    else {
        const auto cmd = args
            | views::join_with(' ')
            | ranges::to<string>();

        Log::logger(Log::Error, "Invalid animation value {} in {}", args[1], cmd);
    }

}


}

void Command::handle_command(string_view cmd) {
    vector<string_view> result = split(cmd);

    if (result.empty()) {
        return;
    }

    if (result[0] == "quit") {
        exit(EXIT_SUCCESS);
    }

    if (result[0] == "animation") {
        if (result.size() <= 2) {
            Log::logger(Log::Error, R"_(Command "animation" requires more args. {})_", cmd);
            return;
        }
        handle_animation(result);
        return;
    }

    if (result[0] == "help") {

        string const help = R"(TIDE ISLAND HELP

Commands:
  quit            Exit Tide Island.
  help            Show this help message.

  animation       Control animations.
    speed <N>     Set animation speed.

    Example:
      animation speed 1.5
)";

        Log::frame_logger(Log::Warning, help);
    }

    else {
        Log::logger(Log::Error, "Invalid command {}", cmd);
    }
}
