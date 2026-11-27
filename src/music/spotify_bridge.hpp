#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <poll.h>

namespace SpotifyBridge {

/// @brief Last favorite state reported by the Spotify extension.
struct State {
	bool        is_connected_{};
	std::string track_uri_{};
	bool        is_known_{};
	bool        is_liked_{};
	bool        is_busy_{};
};

/// @brief Reads the bridge configuration and starts listening if valid.
void init();

/// @brief Re-reads the configuration when its file changed.
void reload();

/// @brief Closes connections and removes the listener.
void shutdown();

/// @brief Returns the current bridge state.
[[nodiscard]] const State& get_state();

/// @brief Asks the extension to set the favorite for uri.
bool set_favorite(std::string_view uri, bool is_liked);

/// @brief Registers the listener invoked when the state changes.
void set_listener(void (*listener)());

/// @brief Copies the descriptors the bridge waits on into out.
void append_poll_fds(std::vector<pollfd>& out);

/// @brief Processes ready descriptors and updates the state.
void process(std::span<const pollfd> ready);

/// @brief Returns true if service names a Spotify MPRIS player.
[[nodiscard]] bool is_spotify_service(std::string_view mpris_service);

/// @brief Derives a Spotify track URI from MPRIS metadata.
[[nodiscard]] std::string
track_uri_from_mpris(std::string_view service, std::string_view track_id, std::string_view media_url);

}
