#pragma once

#include "lyricsmpris/Client.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <poll.h>
#include <span>
#include <vector>

namespace Media {

enum class Update : std::uint8_t {
    None    = 0,
    State   = 1,
    Artwork = 2,
};

[[nodiscard]] constexpr Update operator|(Update a, Update b) noexcept {
    return static_cast<Update>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

[[nodiscard]] constexpr bool contains(Update set, Update u) noexcept {
    return (static_cast<unsigned>(set) & static_cast<unsigned>(u)) != 0;
}

void init();

void shutdown();

[[nodiscard]] bool available();

[[nodiscard]] const lyricsmpris::State* state();

[[nodiscard]] lyricsmpris::Milliseconds position();

[[nodiscard]] std::span<const std::uint8_t> artwork();

void play_pause();

void next();

void previous();

void set_listener(void (*listener)(Update));

void append_poll_fds(std::vector<pollfd>& out);

[[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_deadline();

bool process(std::span<const pollfd> ready);

}