#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <poll.h>

#include "lyrics.hpp"

namespace LyricsMpris {

/// @brief Steady clock used for position and deadline calculations.
using Clock = std::chrono::steady_clock;

/// @brief Playback state reported by the active player.
enum class Playback : uint8_t {
	Stopped,
	Paused,
	Playing,
};

/// @brief State of the lyrics search pipeline.
enum class SearchState : uint8_t {
	Idle,
	Searching,
	Retrying,
	Finished,
	NotFound,
	Failed,
};

/// @brief Lyrics provider.
enum class Provider : uint8_t {
	Lrclib,
	Lrcx,
	Netease,
	Qq,
	Kugou,
	Musixmatch,
};

/// @brief Error category.
enum class ErrorCode : uint8_t {
	Bus,
	Network,
	NoPlayer,
	Unsupported,
	InvalidOptions,
};

/// @brief Error reported by the client.
struct Error {
	ErrorCode   code_{};
	int         native_code_{};
	std::string message_{};
};

/// @brief Player capabilities exposed through MPRIS.
struct Capabilities {
	bool can_control_{};
	bool can_play_{};
	bool can_pause_{};
	bool can_next_{};
	bool can_previous_{};
};

/// @brief Snapshot of the active player's state.
struct State {
	bool                     has_player_{};
	std::string              service_{};
	std::string              track_id_{};
	std::string              title_{};
	std::vector<std::string> artists_{};
	std::string              album_{};
	// MPRIS artwork URI: https://, file://, etc. Image decoding belongs to UI.
	std::string                 artwork_url_{};
	std::string                 media_url_{};
	std::optional<Milliseconds> duration_{};
	Playback                    playback_{Playback::Stopped};
	double                      playback_rate_{1.0};
	Capabilities                capabilities_{};
	// Snapshot at sampled_at_. Use Client::position() for current progress.
	Milliseconds          position_{};
	Clock::time_point     sampled_at_{};
	SearchState           lyrics_search_{SearchState::Idle};
	std::optional<size_t> current_line_{};
	std::optional<Error>  last_error_{};
	uint64_t              track_revision_{};
};

/// @brief Bitmask of state changes produced by Client::process().
enum class Changes : uint16_t {
	None         = 0,
	Player       = 1,
	Track        = 2,
	Playback     = 4,
	Position     = 8,
	Lyrics       = 16,
	Line         = 32,
	Search       = 64,
	Error        = 128,
	Capabilities = 256,
};

/// @brief Combines change flags.
[[nodiscard]] constexpr Changes operator|(Changes a, Changes b) {
	return static_cast<Changes>(std::to_underlying(a) | std::to_underlying(b));
}

/// @brief Returns true when flag is set in changes.
[[nodiscard]] constexpr bool has_flag(Changes changes, Changes flag) {
	return (std::to_underlying(changes) & std::to_underlying(flag)) != 0;
}

/// @brief Client configuration.
struct Options {
	std::string preferred_service_{};
	// Case-sensitive fragments of the MPRIS service name.
	std::vector<std::string> blocked_services_{};
	std::vector<Provider>    providers_{Provider::Lrclib,
		Provider::Lrcx,
		Provider::Netease,
		Provider::Qq,
		Provider::Kugou};
	std::string              musixmatch_api_key_{};
	bool                     should_use_local_lyrics_{true};
	size_t                   max_response_bytes_{1024 * 1024};
	size_t                   max_lyric_lines_{10000};
	unsigned                 max_connections_{2};
	Milliseconds             request_timeout_{7000};
	unsigned                 max_search_attempts_{3};
	Milliseconds             retry_delay_{2500};
	Milliseconds             lyric_offset_{};
	// Optional compatibility resync. Zero disables periodic D-Bus reads.
	Milliseconds position_resync_interval_{};
};

/// @brief Single-thread-owned, move-only module. No UI dependency or owned event loop.
class Client {
public:
	/// @brief Creates a client connected to the session bus.
	[[nodiscard]] static std::expected<Client, Error> create(Options options = {});
	/// @brief Destroys the client and releases its implementation.
	~Client();
	/// @brief Moves the client.
	Client(Client&&) noexcept;
	/// @brief Move-assigns the client.
	Client& operator=(Client&&) noexcept;
	/// @brief Disables copy construction.
	Client(const Client&) = delete;
	/// @brief Disables copy assignment.
	Client& operator=(const Client&) = delete;

	/// @brief Returns the current player state snapshot.
	[[nodiscard]] const State& get_state() const;
	/// @brief Returns the current lyrics document.
	[[nodiscard]] const Lyrics& get_lyrics() const;
	/// @brief Returns the estimated playback position at now.
	[[nodiscard]] Milliseconds position(Clock::time_point now = Clock::now()) const;
	/// @brief Returns the text of the currently active lyric line.
	[[nodiscard]] std::string_view get_current_lyric() const;

	// Copy these interests into the host's poll array. Do not keep borrowed
	// views/references across process(), select_player(), retry_lyrics(), etc.
	/// @brief Returns the poll descriptors the client is interested in.
	[[nodiscard]] std::span<const pollfd> get_poll_fds();
	/// @brief Returns the next time process() should run, if any.
	[[nodiscard]] std::optional<Clock::time_point> get_next_deadline() const;
	// Nonblocking. Call after readiness, or when get_next_deadline() expires;
	// an empty span is sufficient for timeouts and initial queued work.
	/// @brief Advances the client and returns the changes it produced.
	[[nodiscard]] std::expected<Changes, Error> process(std::span<const pollfd> ready = {},
		Clock::time_point                                                       now   = Clock::now());

	/// @brief Selects the MPRIS player with the given service name.
	[[nodiscard]] std::expected<void, Error> select_player(std::string_view service);
	/// @brief Requests another lyrics search for the current track.
	void retry_lyrics();
	// One asynchronous Get(Position) for the active player, e.g. when a progress
	// bar becomes visible. Players do not always emit Seeked; this corrects drift.
	/// @brief Asks the active player for its current position.
	void resync_position();
	/// @brief Sets the lyric timing offset in milliseconds.
	void set_lyric_offset(Milliseconds offset);
	/// @brief Sends Play to the active player.
	[[nodiscard]] std::expected<void, Error> play();
	/// @brief Sends Pause to the active player.
	[[nodiscard]] std::expected<void, Error> pause();
	/// @brief Sends PlayPause to the active player.
	[[nodiscard]] std::expected<void, Error> play_pause();
	/// @brief Skips to the next track.
	[[nodiscard]] std::expected<void, Error> next();
	/// @brief Returns to the previous track.
	[[nodiscard]] std::expected<void, Error> previous();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_{};
	explicit Client(std::unique_ptr<Impl> impl);
};

}
