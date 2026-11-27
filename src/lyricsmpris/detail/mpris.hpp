#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <systemd/sd-bus.h>

#include "../client.hpp"

namespace LyricsMpris::Detail {

/// @brief Snapshot of one discovered MPRIS player.
struct Player {
	State       state_{};
	std::string owner_{};
	std::string inline_lyrics_{};
	size_t      lyrics_hash_{};
	uint64_t    instance_{};
	uint64_t    properties_revision_{};
	uint64_t    track_revision_{};
	uint64_t    last_active_{};
	bool        is_valid_{};
	bool        are_properties_pending_{};
	bool        is_position_pending_{};
};

/// @brief Estimates playback position from the last sampled state.
Milliseconds estimated_position(const State& state, Clock::time_point now);

/// @brief Reads player metadata and sends MPRIS playback commands.
class Mpris {
public:
	/// @brief Creates a client connected to the session bus.
	static std::expected<std::unique_ptr<Mpris>, Error> create(const Options& options);
	/// @brief Releases bus matches and pending calls.
	~Mpris();
	/// @brief Drains pending D-Bus messages.
	std::expected<void, Error> process();
	/// @brief Returns the D-Bus descriptor to poll.
	std::expected<pollfd, Error> get_poll_fd() const;
	/// @brief Returns the next D-Bus deadline, if any.
	std::optional<Clock::time_point> get_deadline() const;

	/// @brief Returns the currently discovered players.
	std::span<const Player> get_players() const { return players_; }

	/// @brief Returns the revision incremented on player state changes.
	uint64_t get_revision() const { return revision_; }

	/// @brief Takes the last bus error, if any.
	std::optional<Error> take_error();
	/// @brief Sends a playback method call to the given player.
	std::expected<void, Error> control(std::string_view service, const char* method);
	/// @brief Requests the current position from the given player.
	void request_position(std::string_view service);

private:
	enum class Call : uint8_t {
		List,
		Owner,
		Properties,
		Position,
		Control
	};
	struct Pending;
	explicit Mpris(const Options& options);
	Player*    find(std::string_view service);
	bool       is_blocked(std::string_view service) const;
	void       discover(std::string_view service, std::string_view owner = {});
	void       request_properties(Player& player);
	int        async(Call  kind,
		Player*     player,
		const char* destination,
		const char* path,
		const char* interface,
		const char* method,
		const char* argument = nullptr);
	int        read_properties(sd_bus_message* message, Player& player);
	int        read_metadata(sd_bus_message* message, Player& player);
	void       reply(sd_bus_message* message, Pending& pending);
	void       signal(sd_bus_message* message);
	void       player_signal(sd_bus_message* message, Player& player);
	static int reply_callback(sd_bus_message*, void*, sd_bus_error*) noexcept;
	static int signal_callback(sd_bus_message*, void*, sd_bus_error*) noexcept;
	static int install_callback(sd_bus_message*, void*, sd_bus_error*) noexcept;

	sd_bus*                               bus_{};
	std::vector<sd_bus_slot*>             matches_{};
	std::vector<std::unique_ptr<Pending>> pending_{};
	std::vector<Player>                   players_{};
	std::vector<std::string>              blocked_{};
	size_t                                max_lyrics_bytes_{};
	uint64_t                              next_instance_{};
	uint64_t                              activity_{};
	uint64_t                              revision_{};
	std::optional<Error>                  error_{};
	bool                                  has_work_remaining_{};
};

}
