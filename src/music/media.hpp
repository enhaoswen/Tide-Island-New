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
    return static_cast<Update>(std::to_underlying(a) | std::to_underlying(b));
}

[[nodiscard]] constexpr bool contains(Update set, Update u) noexcept {
    return (std::to_underlying(set) & std::to_underlying(u)) != 0;
}

void init();

void shutdown();

[[nodiscard]] bool available();

[[nodiscard]] const lyricsmpris::State* state();

[[nodiscard]] lyricsmpris::Milliseconds position();

// Current cover, center-cropped and downscaled to at most k_artwork_side square.
// nullptr when there is none (yet).
inline constexpr int k_artwork_side = 120;
[[nodiscard]] const Image::Pixels* artwork();

// Re-reads the player's position once (asynchronously); call when progress
// becomes visible. The result arrives as an Update::State.
void resync_position();

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