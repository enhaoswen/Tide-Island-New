#pragma once

#include "lyricsmpris/Client.hpp"

#include "graphics/image.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <poll.h>
#include <span>
#include <vector>

namespace Media {

enum class Update : std::uint8_t {
    None    = 0,
    State   = 1, // track, playback, position, capabilities, errors
    Artwork = 2, // artwork() changed (possibly to none)
    Lyrics  = 4, // lyrics document / current line / search state
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

// Current cover, center-cropped and downscaled to at most kArtworkSide square.
// nullptr when there is none (yet).
inline constexpr int kArtworkSide = 120;
[[nodiscard]] const Image::Pixels* artwork();

void play_pause();

void next();

void previous();

void set_listener(void (*listener)(Update));

void append_poll_fds(std::vector<pollfd>& out);

[[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_deadline();

// Changes are reported through the listener only; whoever reacts to them
// requests a redraw, so updates nobody displays cost no frame.
void process(std::span<const pollfd> ready);

}