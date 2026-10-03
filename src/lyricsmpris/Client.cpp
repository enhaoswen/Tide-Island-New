#include "Client.hpp"
#include "detail/Mpris.hpp"
#include "detail/Resolver.hpp"
#include "detail/Utility.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace lyricsmpris {
namespace {

void mark(Changes& changes, Changes flag) { changes = changes | flag; }

bool valid_options(const Options& options) {
    return options.max_response_bytes > 0 && options.max_response_bytes <= 8 * 1024 * 1024
        && options.max_lyric_lines > 0 && options.max_lyric_lines <= 100000
        && options.max_connections > 0 && options.max_connections <= 8
        && options.request_timeout > Milliseconds{} && options.request_timeout <= Milliseconds{600000}
        && options.max_search_attempts > 0 && options.max_search_attempts <= 10
        && options.retry_delay > Milliseconds{} && options.retry_delay <= Milliseconds{86400000}
        && options.position_resync_interval >= Milliseconds{}
        && options.position_resync_interval <= Milliseconds{86400000}
        && options.lyric_offset >= Milliseconds{-3600000} && options.lyric_offset <= Milliseconds{3600000}
        && options.providers.size() <= 6
        && std::all_of(options.providers.begin(), options.providers.end(), [](auto provider) {
            return provider >= Provider::Lrclib && provider <= Provider::Musixmatch;
        });
}

bool metadata_equal(const State& a, const State& b) {
    return std::tie(a.has_player, a.service, a.track_id, a.title, a.artists, a.album, a.artwork_url, a.media_url, a.duration)
        == std::tie(b.has_player, b.service, b.track_id, b.title, b.artists, b.album, b.artwork_url, b.media_url, b.duration);
}

bool capabilities_equal(const Capabilities& a, const Capabilities& b) {
    return std::tie(a.control, a.play, a.pause, a.next, a.previous)
        == std::tie(b.control, b.play, b.pause, b.next, b.previous);
}

detail::TrackQuery query_for(const State& state) {
    detail::TrackQuery query;
    query.title = state.title; query.album = state.album; query.duration = state.duration;
    for (const auto& artist : state.artists) {
        if (!query.artist.empty()) query.artist += ", ";
        query.artist += artist;
    }
    return query;
}

} // namespace

struct Client::Impl {
    Options options;
    std::unique_ptr<detail::Mpris> mpris;
    std::unique_ptr<detail::Resolver> resolver;
    State state;
    std::vector<pollfd> fds;
    std::uint64_t bus_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t document_revision = 0;
    std::uint64_t player_instance = 0;
    std::size_t inline_hash = 0;
    bool selection_dirty = true;
    bool retry_requested = false;
    bool offset_dirty = false;
    std::optional<Clock::time_point> resync_at;
    std::optional<Error> poll_error;

    explicit Impl(Options value) : options(std::move(value)) {}

    const detail::Player* choose() const {
        const auto players = mpris->players();
        const detail::Player* current = nullptr;
        for (const auto& player : players) {
            if (!player.valid) continue;
            if (!options.preferred_service.empty() && player.state.service == options.preferred_service) return &player;
            if (player.state.service == state.service && player.instance == player_instance) current = &player;
        }
        // Keep an already selected playing player stable; choose the most
        // recently started player when the selected one stops/pauses/disappears.
        if (current && current->state.playback == Playback::Playing) return current;
        const detail::Player* best = nullptr;
        auto rank = [](const detail::Player& player) {
            return player.state.playback == Playback::Playing ? 2 : player.state.playback == Playback::Paused ? 1 : 0;
        };
        for (const auto& player : players) {
            if (!player.valid) continue;
            if (!best || rank(player) > rank(*best)
                || (rank(player) == rank(*best) && player.last_active > best->last_active)
                || (rank(player) == rank(*best) && player.last_active == best->last_active && player.state.service < best->state.service)) best = &player;
        }
        if (current && best && rank(*current) == rank(*best) && current->state.playback == Playback::Paused) return current;
        return best;
    }

    void refresh(Changes& changes, bool force_retry) {
        const auto* player = choose();
        if (!player) {
            if (state.has_player) {
                const auto revision = state.track_revision + 1;
                state = {};
                state.track_revision = revision;
                resolver->clear();
                player_instance = 0;
                inline_hash = 0;
                mark(changes, Changes::Player | Changes::Track | Changes::Playback | Changes::Position);
            }
            resync_at.reset();
            return;
        }
        const auto& incoming = player->state;
        const bool new_player = player_instance != player->instance || state.service != incoming.service;
        const bool new_track = new_player || state.track_id != incoming.track_id || state.title != incoming.title
            || state.artists != incoming.artists;
        const bool changed_query = state.album != incoming.album || state.duration != incoming.duration;
        const bool late_url = state.media_url != incoming.media_url && !incoming.media_url.empty();
        const auto hash = player->lyrics_hash;
        const bool late_lyrics = hash != inline_hash && !player->inline_lyrics.empty();
        const bool changed_playback = state.playback != incoming.playback || state.playback_rate != incoming.playback_rate;
        if (new_player) mark(changes, Changes::Player);
        if (!metadata_equal(state, incoming)) mark(changes, Changes::Track);
        if (changed_playback) mark(changes, Changes::Playback);
        if (!capabilities_equal(state.capabilities, incoming.capabilities)) mark(changes, Changes::Capabilities);
        if (state.position != incoming.position || state.sampled_at != incoming.sampled_at) mark(changes, Changes::Position);
        const auto revision = state.track_revision + (new_track ? 1 : 0);
        const auto search = state.lyrics_search;
        const auto line = state.current_line;
        auto error = std::move(state.last_error);
        state = incoming;
        state.track_revision = revision;
        state.lyrics_search = search;
        state.current_line = line;
        state.last_error = std::move(error);
        player_instance = player->instance;
        inline_hash = hash;
        const bool restart_query = changed_query && !resolver->document().synced();
        if (new_track || force_retry || late_lyrics || restart_query || (!resolver->document().synced() && late_url)) {
            state.last_error.reset();
            resolver->start(query_for(state), player->inline_lyrics, state.media_url, new_track || restart_query);
        }
        if (options.position_resync_interval > Milliseconds{} && state.playback == Playback::Playing) {
            if (!resync_at || new_player || changed_playback) resync_at = Clock::now() + options.position_resync_interval;
        } else resync_at.reset();
    }

    std::expected<void, Error> command(const char* method) {
        const auto* player = choose();
        if (!player) return std::unexpected(Error{ErrorCode::NoPlayer, 0, "No active MPRIS player"});
        const auto& capabilities = player->state.capabilities;
        bool supported = capabilities.control;
        const std::string_view operation = method;
        if (operation == "Play") supported &= capabilities.play;
        else if (operation == "Pause") supported &= capabilities.pause;
        else if (operation == "Next") supported &= capabilities.next;
        else if (operation == "Previous") supported &= capabilities.previous;
        else if (operation == "PlayPause") supported &= player->state.playback == Playback::Playing ? capabilities.pause : capabilities.play;
        if (!supported) return std::unexpected(Error{ErrorCode::Unsupported, 0, "Player does not support " + std::string(operation)});
        return mpris->control(player->state.service, method);
    }
};

Client::Client(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Client::~Client() = default;
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;

std::expected<Client, Error> Client::create(Options options) {
    if (!valid_options(options)) return std::unexpected(Error{ErrorCode::InvalidOptions, 0, "Invalid lyricsmpris options or resource limits"});
    // Duplicate providers do not trigger repeated requests.
    std::vector<Provider> providers;
    for (auto provider : options.providers)
        if (std::find(providers.begin(), providers.end(), provider) == providers.end()) providers.push_back(provider);
    options.providers = std::move(providers);
    auto impl = std::make_unique<Impl>(std::move(options));
    auto mpris = detail::Mpris::create(impl->options);
    if (!mpris) return std::unexpected(std::move(mpris.error()));
    impl->mpris = std::move(*mpris);
    auto resolver = detail::Resolver::create(impl->options);
    if (!resolver) return std::unexpected(std::move(resolver.error()));
    impl->resolver = std::move(*resolver);
    impl->fds.reserve(1 + impl->options.max_connections * 2);
    return Client(std::move(impl));
}

const State& Client::state() const noexcept { return impl_->state; }
const Lyrics& Client::lyrics() const noexcept { return impl_->resolver->document(); }

Milliseconds Client::position(Clock::time_point now) const noexcept {
    return detail::estimated_position(impl_->state, now);
}

std::string_view Client::current_lyric() const noexcept {
    const auto index = impl_->state.current_line;
    return index ? lyrics().line(*index).text : std::string_view{};
}

std::span<const pollfd> Client::poll_fds() {
    impl_->fds.clear();
    if (auto fd = impl_->mpris->poll_fd()) impl_->fds.push_back(*fd);
    else impl_->poll_error = std::move(fd.error());
    const auto http_fds = impl_->resolver->poll_fds();
    impl_->fds.insert(impl_->fds.end(), http_fds.begin(), http_fds.end());
    return impl_->fds;
}

std::optional<Clock::time_point> Client::next_deadline() const noexcept {
    const auto now = Clock::now();
    if (impl_->selection_dirty || impl_->retry_requested || impl_->offset_dirty || impl_->poll_error) return now;
    auto deadline = impl_->mpris->deadline();
    auto include = [&](std::optional<Clock::time_point> value) {
        if (value && (!deadline || *value < *deadline)) deadline = value;
    };
    include(impl_->resolver->deadline());
    include(impl_->resync_at);
    const auto& state = impl_->state;
    const auto& document = lyrics();
    const auto progress = position(now);
    if (state.playback == Playback::Playing && document.synced() && document.size()
        && !(state.playback_rate > 0 && state.duration && progress >= *state.duration)
        && !(state.playback_rate < 0 && progress <= Milliseconds{})) {
        const auto lyric_position = progress - impl_->options.lyric_offset;
        const auto current = document.line_at(lyric_position);
        std::optional<Milliseconds> boundary;
        if (state.playback_rate > 0) {
            const auto next = current ? *current + 1 : 0;
            if (next < document.size()) boundary = document.line(next).start_time;
        } else if (current) boundary = *document.line(*current).start_time - Milliseconds{1};
        if (boundary) {
            const auto delay = (static_cast<long double>(boundary->count()) - lyric_position.count()) / state.playback_rate;
            // Ceiling prevents a busy loop caused by sub-millisecond rounding.
            if (delay >= 0 && delay <= 86400000) {
                const auto due = now + Milliseconds{std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(delay)))};
                if (!state.duration || state.playback_rate < 0
                    || static_cast<long double>(boundary->count()) + impl_->options.lyric_offset.count() <= state.duration->count())
                    include(due);
            }
        }
    }
    return deadline;
}

std::expected<Changes, Error> Client::process(std::span<const pollfd> ready, Clock::time_point now) {
    auto& self = *impl_;
    if (self.poll_error) return std::unexpected(*self.poll_error);
    Changes changes = Changes::None;
    if (auto result = self.mpris->process(); !result) return std::unexpected(std::move(result.error()));
    const bool retry = self.retry_requested || self.resolver->retry_due(now);
    if (self.retry_requested) self.resolver->reset_attempts();
    if (self.selection_dirty || retry || self.bus_revision != self.mpris->revision()) {
        self.refresh(changes, retry);
        self.bus_revision = self.mpris->revision();
        self.selection_dirty = false;
        self.retry_requested = false;
    }
    if (self.resync_at && now >= *self.resync_at) {
        self.mpris->request_position(self.state.service);
        self.resync_at = now + self.options.position_resync_interval;
    }
    if (auto result = self.resolver->process(ready, now); !result) return std::unexpected(std::move(result.error()));
    if (self.document_revision != self.resolver->document_revision()) {
        self.document_revision = self.resolver->document_revision();
        mark(changes, Changes::Lyrics | Changes::Line);
    }
    if (self.state.lyrics_search != self.resolver->state()) {
        self.state.lyrics_search = self.resolver->state();
        mark(changes, Changes::Search);
    }
    auto next_line = lyrics().line_at(position(now) - self.options.lyric_offset);
    if (next_line != self.state.current_line || self.offset_dirty) {
        self.state.current_line = next_line;
        self.offset_dirty = false;
        mark(changes, Changes::Line);
    }
    for (auto error : {self.mpris->take_error(), self.resolver->take_error()}) {
        if (error) { self.state.last_error = std::move(error); mark(changes, Changes::Error); }
    }
    return changes;
}

std::expected<void, Error> Client::select_player(std::string_view service) {
    if (!service.empty()) {
        if (!detail::is_service(service)) return std::unexpected(Error{ErrorCode::InvalidOptions, 0, "Expected a full MPRIS service name"});
        const auto players = impl_->mpris->players();
        if (std::none_of(players.begin(), players.end(), [&](const auto& player) { return player.valid && player.state.service == service; }))
            return std::unexpected(Error{ErrorCode::NoPlayer, 0, "Requested player is unavailable"});
    }
    impl_->options.preferred_service = service;
    impl_->selection_dirty = true;
    return {};
}

void Client::retry_lyrics() { impl_->retry_requested = true; }

void Client::set_lyric_offset(Milliseconds offset) noexcept {
    impl_->options.lyric_offset = std::clamp(offset, Milliseconds{-3600000}, Milliseconds{3600000});
    impl_->offset_dirty = true;
}

std::expected<void, Error> Client::play() { return impl_->command("Play"); }
std::expected<void, Error> Client::pause() { return impl_->command("Pause"); }
std::expected<void, Error> Client::play_pause() { return impl_->command("PlayPause"); }
std::expected<void, Error> Client::next() { return impl_->command("Next"); }
std::expected<void, Error> Client::previous() { return impl_->command("Previous"); }

} // namespace lyricsmpris
