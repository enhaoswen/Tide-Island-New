#pragma once

#include "../Client.hpp"

#include <systemd/sd-bus.h>

namespace lyricsmpris::detail {

struct Player {
    State state;
    std::string owner;
    std::string inline_lyrics;
    std::size_t lyrics_hash = 0;
    std::uint64_t instance = 0;
    std::uint64_t properties_revision = 0;
    std::uint64_t track_revision = 0;
    std::uint64_t last_active = 0;
    bool valid = false;
    bool properties_pending = false;
    bool position_pending = false;
};

Milliseconds estimated_position(const State& state, Clock::time_point now) noexcept;

class Mpris {
public:
    static std::expected<std::unique_ptr<Mpris>, Error> create(const Options& options);
    ~Mpris();
    std::expected<void, Error> process();
    std::expected<pollfd, Error> poll_fd() const;
    std::optional<Clock::time_point> deadline() const noexcept;
    std::span<const Player> players() const noexcept { return players_; }
    std::uint64_t revision() const noexcept { return revision_; }
    std::optional<Error> take_error();
    std::expected<void, Error> control(std::string_view service, const char* method);
    void request_position(std::string_view service);

private:
    enum class Call { List, Owner, Properties, Position, Control };
    struct Pending;
    explicit Mpris(const Options& options);
    Player* find(std::string_view service);
    bool blocked(std::string_view service) const;
    void discover(std::string_view service, std::string_view owner = {});
    void request_properties(Player& player);
    int async(Call kind, Player* player, const char* destination, const char* path,
        const char* interface, const char* method, const char* argument = nullptr);
    int read_properties(sd_bus_message* message, Player& player);
    int read_metadata(sd_bus_message* message, Player& player);
    void reply(sd_bus_message* message, Pending& pending);
    void signal(sd_bus_message* message);
    void player_signal(sd_bus_message* message, Player& player);
    static int reply_callback(sd_bus_message*, void*, sd_bus_error*) noexcept;
    static int signal_callback(sd_bus_message*, void*, sd_bus_error*) noexcept;
    static int install_callback(sd_bus_message*, void*, sd_bus_error*) noexcept;

    sd_bus* bus_ = nullptr;
    std::vector<sd_bus_slot*> matches_;
    std::vector<std::unique_ptr<Pending>> pending_;
    std::vector<Player> players_;
    std::vector<std::string> blocked_;
    std::size_t max_lyrics_bytes_;
    std::uint64_t next_instance_ = 0;
    std::uint64_t activity_ = 0;
    std::uint64_t revision_ = 0;
    std::optional<Error> error_;
    bool work_remaining_ = false;
};

} // namespace lyricsmpris::detail
