#pragma once

#include "Lyrics.hpp"

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <poll.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lyricsmpris {

using Clock = std::chrono::steady_clock;

enum class Playback { Stopped, Paused, Playing };
enum class SearchState { Idle, Searching, Retrying, Finished, NotFound, Failed };
enum class Provider { Lrclib, Lrcx, Netease, Qq, Kugou, Musixmatch };
enum class ErrorCode { Bus, Network, NoPlayer, Unsupported, InvalidOptions };

struct Error {
    ErrorCode code;
    int native_code = 0;
    std::string message;
};

struct Capabilities {
    bool control = false;
    bool play = false;
    bool pause = false;
    bool next = false;
    bool previous = false;
};

struct State {
    bool has_player = false;
    std::string service;
    std::string track_id;
    std::string title;
    std::vector<std::string> artists;
    std::string album;
    // MPRIS artwork URI: https://, file://, etc. Image decoding belongs to UI.
    std::string artwork_url;
    std::string media_url;
    std::optional<Milliseconds> duration;
    Playback playback = Playback::Stopped;
    double playback_rate = 1.0;
    Capabilities capabilities;
    // Snapshot at sampled_at. Use Client::position() for current progress.
    Milliseconds position{};
    Clock::time_point sampled_at{};
    SearchState lyrics_search = SearchState::Idle;
    std::optional<std::size_t> current_line;
    std::optional<Error> last_error;
    std::uint64_t track_revision = 0;
};

enum class Changes : unsigned {
    None = 0, Player = 1, Track = 2, Playback = 4, Position = 8,
    Lyrics = 16, Line = 32, Search = 64, Error = 128, Capabilities = 256
};
constexpr Changes operator|(Changes a, Changes b) noexcept {
    return static_cast<Changes>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr bool contains(Changes changes, Changes flag) noexcept {
    return (static_cast<unsigned>(changes) & static_cast<unsigned>(flag)) != 0;
}

struct Options {
    std::string preferred_service;
    // Case-sensitive fragments of the MPRIS service name.
    std::vector<std::string> blocked_services;
    std::vector<Provider> providers = {
        Provider::Lrclib, Provider::Lrcx, Provider::Netease, Provider::Qq, Provider::Kugou
    };
    std::string musixmatch_api_key;
    bool local_lyrics = true;
    std::size_t max_response_bytes = 1024 * 1024;
    std::size_t max_lyric_lines = 10000;
    unsigned max_connections = 2;
    Milliseconds request_timeout{7000};
    unsigned max_search_attempts = 3;
    Milliseconds retry_delay{2500};
    Milliseconds lyric_offset{};
    // Optional compatibility resync. Zero disables periodic D-Bus reads.
    Milliseconds position_resync_interval{};
};

// Single-thread-owned, move-only module. No UI dependency or owned event loop.
class Client {
public:
    [[nodiscard]] static std::expected<Client, Error> create(Options options = {});
    ~Client();
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    [[nodiscard]] const State& state() const noexcept;
    [[nodiscard]] const Lyrics& lyrics() const noexcept;
    [[nodiscard]] Milliseconds position(Clock::time_point now = Clock::now()) const noexcept;
    [[nodiscard]] std::string_view current_lyric() const noexcept;

    // Copy these interests into the host's poll array. Do not keep borrowed
    // views/references across process(), select_player(), retry_lyrics(), etc.
    [[nodiscard]] std::span<const pollfd> poll_fds();
    [[nodiscard]] std::optional<Clock::time_point> next_deadline() const noexcept;
    // Nonblocking. Call after readiness, or when next_deadline() expires;
    // an empty span is sufficient for timeouts and initial queued work.
    [[nodiscard]] std::expected<Changes, Error> process(
        std::span<const pollfd> ready = {}, Clock::time_point now = Clock::now());

    [[nodiscard]] std::expected<void, Error> select_player(std::string_view service);
    void retry_lyrics();
    void set_lyric_offset(Milliseconds offset) noexcept;
    [[nodiscard]] std::expected<void, Error> play();
    [[nodiscard]] std::expected<void, Error> pause();
    [[nodiscard]] std::expected<void, Error> play_pause();
    [[nodiscard]] std::expected<void, Error> next();
    [[nodiscard]] std::expected<void, Error> previous();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit Client(std::unique_ptr<Impl> impl) noexcept;
};

} // namespace lyricsmpris
