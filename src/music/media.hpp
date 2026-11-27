#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <poll.h>

#include "graphics/image.hpp"
#include "lyricsmpris/client.hpp"

namespace Media {

/// @brief Bitmask of media changes reported to the listener.
enum class Update : uint8_t {
	None    = 0,
	State   = 1, // track, playback, position, capabilities, errors
	Artwork = 2, // get_artwork() changed (possibly to none)
	Lyrics  = 4, // lyrics document / current line / search state
};

/// @brief Combines two update flags.
[[nodiscard]] constexpr Update operator|(Update a, Update b) {
	return static_cast<Update>(std::to_underlying(a) | std::to_underlying(b));
}

/// @brief Returns true if flag u is set in set.
[[nodiscard]] constexpr bool has_flag(Update set, Update u) {
	return (std::to_underlying(set) & std::to_underlying(u)) != 0;
}

/// @brief Initializes the MPRIS client.
void init();

/// @brief Stops the client, releases artwork and removes the listener.
void shutdown();

/// @brief Returns true if an MPRIS client is available.
[[nodiscard]] bool is_available();

/// @brief Returns the current player state snapshot, or nullptr.
[[nodiscard]] const LyricsMpris::State* get_state();

/// @brief Returns the estimated playback position.
[[nodiscard]] LyricsMpris::Milliseconds get_position();

/// @brief Side length in pixels of the cached cover image.
inline constexpr int k_artwork_side{120};

/// @brief Returns the current cover image, or nullptr when there is none (yet).
// Center-cropped and downscaled to at most k_artwork_side square.
[[nodiscard]] const Image::Pixels* get_artwork();

/// @brief Re-reads the player's position once (asynchronously).
// The result arrives as an Update::State; call when progress becomes visible.
void resync_position();

/// @brief Toggles playback between play and pause.
void play_pause();

/// @brief Skips to the next track.
void next();

/// @brief Returns to the previous track.
void previous();

/// @brief Registers the listener invoked with changed media aspects.
void set_listener(void (*listener)(Update));

/// @brief Copies the descriptors media waits on into out.
void append_poll_fds(std::vector<pollfd>& out);

/// @brief Returns the next time process() should run, if any.
[[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_deadline();

/// @brief Processes ready descriptors and reports changes through the listener.
// Whoever reacts to them requests a redraw, so updates nobody displays cost no frame.
void process(std::span<const pollfd> ready);

}
