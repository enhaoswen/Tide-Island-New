#pragma once

#include <string_view>

namespace Command {

/// @brief Handles a single command line read from stdin.
void handle_command(std::string_view cmd);

}
