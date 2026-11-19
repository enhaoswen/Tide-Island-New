#pragma once

#include <poll.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace SpotifyBridge {

struct State {
    bool        connected{};
    std::string track_uri{};
    bool        known{};
    bool        liked{};
    bool        busy{};
};

void init();

void reload();

void shutdown();

[[nodiscard]] const State& state();

bool set_favorite(std::string_view uri, bool liked);

void set_listener(void (*listener)());

void append_poll_fds(std::vector<pollfd>& out);

void process(std::span<const pollfd> ready);

[[nodiscard]] bool is_spotify_service(std::string_view mpris_service);

[[nodiscard]] std::string track_uri_from_mpris(
    std::string_view service, std::string_view track_id, std::string_view media_url);

}