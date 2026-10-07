#include "Mpris.hpp"
#include "Utility.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <time.h>

using namespace std;

namespace lyricsmpris::detail {
namespace {

constexpr auto player_path = "/org/mpris/MediaPlayer2";
constexpr auto player_interface = "org.mpris.MediaPlayer2.Player";
constexpr auto properties_interface = "org.freedesktop.DBus.Properties";

Error bus_error(int code, string_view message = {}) {
    return {ErrorCode::Bus, code, message.empty() ? strerror(code < 0 ? -code : code) : string(message)};
}

string bounded(const char* value, size_t limit = 4096) {
    if (!value) return {};
    const auto length = strnlen(value, limit + 1);
    return length > limit ? string{} : string(value, length);
}

Playback playback(string_view value) {
    if (value == "Playing") return Playback::Playing;
    if (value == "Paused") return Playback::Paused;
    return Playback::Stopped;
}

} // namespace

Milliseconds estimated_position(const State& state, Clock::time_point now) noexcept {
    long double value = static_cast<long double>(state.position.count());
    if (state.playback == Playback::Playing && now > state.sampled_at) {
        const auto elapsed = chrono::duration<long double, milli>(now - state.sampled_at).count();
        value += elapsed * state.playback_rate;
    }
    const auto maximum = state.duration ? state.duration->count() : numeric_limits<int64_t>::max();
    value = clamp(value, 0.0L, static_cast<long double>(maximum));
    return Milliseconds{static_cast<int64_t>(value)};
}

struct Mpris::Pending {
    Mpris* self;
    Call kind;
    string service;
    uint64_t instance = 0;
    uint64_t revision = 0;
    uint64_t track = 0;
    sd_bus_slot* slot = nullptr;
    bool done = false;
    ~Pending() { sd_bus_slot_unref(slot); }
};

Mpris::Mpris(const Options& options)
    : blocked_(options.blocked_services), max_lyrics_bytes_(options.max_response_bytes) {
    matches_.reserve(3);
    players_.reserve(4);
}

expected<unique_ptr<Mpris>, Error> Mpris::create(const Options& options) {
    auto self = unique_ptr<Mpris>(new Mpris(options));
    int result = sd_bus_open_user(&self->bus_);
    if (result < 0) return unexpected(bus_error(result));
    sd_bus_set_method_call_timeout(self->bus_, 1000000);
    for (const auto* match : {
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'",
        "type='signal',path='/org/mpris/MediaPlayer2',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
        "type='signal',path='/org/mpris/MediaPlayer2',interface='org.mpris.MediaPlayer2.Player',member='Seeked'"
    }) {
        sd_bus_slot* slot = nullptr;
        result = sd_bus_add_match_async(self->bus_, &slot, match,
            &Mpris::signal_callback, &Mpris::install_callback, self.get());
        if (result < 0) return unexpected(bus_error(result));
        self->matches_.push_back(slot);
    }
    result = self->async(Call::List, nullptr, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "ListNames");
    if (result < 0) return unexpected(bus_error(result));
    return self;
}

Mpris::~Mpris() {
    pending_.clear();
    for (auto* slot : matches_) sd_bus_slot_unref(slot);
    sd_bus_close_unref(bus_);
}

Player* Mpris::find(string_view service) {
    const auto at = find_if(players_.begin(), players_.end(),
        [&](const Player& player) { return player.state.service == service; });
    return at == players_.end() ? nullptr : &*at;
}

bool Mpris::blocked(string_view service) const {
    return any_of(blocked_.begin(), blocked_.end(), [&](const auto& fragment) {
        return !fragment.empty() && service.find(fragment) != service.npos;
    });
}

int Mpris::async(Call kind, Player* player, const char* destination, const char* path,
    const char* interface, const char* method, const char* argument) {
    if (pending_.size() >= 128) return -ENOBUFS;
    auto call = make_unique<Pending>();
    call->self = this;
    call->kind = kind;
    if (player) {
        call->service = player->state.service;
        call->instance = player->instance;
        call->revision = player->properties_revision;
        call->track = player->track_revision;
    }
    int result;
    if (kind == Call::Position) {
        result = sd_bus_call_method_async(bus_, &call->slot, destination, path, interface, method,
            &Mpris::reply_callback, call.get(), "ss", player_interface, "Position");
    } else {
        result = sd_bus_call_method_async(bus_, &call->slot, destination, path, interface, method,
            &Mpris::reply_callback, call.get(), argument ? "s" : "", argument);
    }
    if (result >= 0) pending_.push_back(move(call));
    return result;
}

void Mpris::discover(string_view service, string_view owner) {
    if (!is_service(service) || blocked(service)) return;
    auto* player = find(service);
    if (!player) {
        if (players_.size() >= 32) return;
        players_.emplace_back();
        player = &players_.back();
        player->state.service = service;
        player->instance = ++next_instance_;
    }
    if (owner.empty()) {
        if (!player->owner.empty()) return;
        const auto result = async(Call::Owner, player, "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "GetNameOwner", player->state.service.c_str());
        if (result < 0) error_ = bus_error(result);
    } else {
        if (player->owner != owner) {
            const auto name = player->state.service;
            *player = Player{};
            player->state.service = name;
            player->owner = owner;
            player->instance = ++next_instance_;
        }
        request_properties(*player);
    }
}

void Mpris::request_properties(Player& player) {
    if (player.properties_pending || player.owner.empty()) return;
    const auto result = async(Call::Properties, &player, player.owner.c_str(), player_path,
        properties_interface, "GetAll", player_interface);
    if (result < 0) error_ = bus_error(result);
    else player.properties_pending = true;
}

void Mpris::request_position(string_view service) {
    auto* player = find(service);
    if (!player || !player->valid || player->position_pending) return;
    const auto result = async(Call::Position, player, player->owner.c_str(), player_path, properties_interface, "Get");
    if (result < 0) error_ = bus_error(result);
    else player->position_pending = true;
}

int Mpris::read_metadata(sd_bus_message* message, Player& player) {
    auto& state = player.state;
    state.track_id.clear(); state.title.clear(); state.artists.clear(); state.album.clear();
    state.artwork_url.clear(); state.media_url.clear(); state.duration.reset();
    player.inline_lyrics.clear();
    string comment;
    int result = sd_bus_message_enter_container(message, 'a', "{sv}");
    if (result < 0) return result;
    while ((result = sd_bus_message_enter_container(message, 'e', "sv")) > 0) {
        const char* key = nullptr;
        if ((result = sd_bus_message_read(message, "s", &key)) < 0) return result;
        const char* signature = nullptr;
        if ((result = sd_bus_message_peek_type(message, nullptr, &signature)) < 0) return result;
        if ((result = sd_bus_message_enter_container(message, 'v', signature)) < 0) return result;
        const string_view name = key;
        if (strcmp(signature, "s") == 0 || strcmp(signature, "o") == 0) {
            const char* text = nullptr;
            if ((result = sd_bus_message_read(message, signature, &text)) < 0) return result;
            if (name == "mpris:trackid") state.track_id = bounded(text);
            else if (name == "xesam:title") state.title = bounded(text);
            else if (name == "xesam:album") state.album = bounded(text);
            else if (name == "mpris:artUrl") state.artwork_url = bounded(text, 16384);
            else if (name == "xesam:url") state.media_url = bounded(text, 16384);
            else if (name == "xesam:asText") player.inline_lyrics = bounded(text, max_lyrics_bytes_);
            else if (name == "xesam:comment") comment = bounded(text, max_lyrics_bytes_);
        } else if (name == "mpris:length" && strcmp(signature, "x") == 0) {
            int64_t length = 0;
            if ((result = sd_bus_message_read(message, "x", &length)) < 0) return result;
            if (length > 0) state.duration = Milliseconds{length / 1000};
        } else if ((name == "xesam:artist" || name == "xesam:comment") && strcmp(signature, "as") == 0) {
            if ((result = sd_bus_message_enter_container(message, 'a', "s")) < 0) return result;
            const char* item = nullptr;
            while ((result = sd_bus_message_read(message, "s", &item)) > 0) {
                if (name == "xesam:artist" && state.artists.size() < 32) state.artists.push_back(bounded(item));
                else if (name == "xesam:comment") {
                    const auto line = bounded(item, max_lyrics_bytes_);
                    if (comment.size() + line.size() + 1 <= max_lyrics_bytes_) { comment += line; comment += '\n'; }
                }
            }
            if (result < 0) return result;
            if ((result = sd_bus_message_exit_container(message)) < 0) return result;
        } else if ((result = sd_bus_message_skip(message, signature)) < 0) return result;
        if ((result = sd_bus_message_exit_container(message)) < 0) return result;
        if ((result = sd_bus_message_exit_container(message)) < 0) return result;
    }
    if (result < 0) return result;
    if (player.inline_lyrics.empty()) player.inline_lyrics = move(comment);
    // Hash large embedded lyrics once per metadata update, not on volume,
    // playback or seek events while the UI is active.
    player.lyrics_hash = hash<string_view>{}(player.inline_lyrics);
    return sd_bus_message_exit_container(message);
}

int Mpris::read_properties(sd_bus_message* message, Player& player) {
    auto& state = player.state;
    const auto now = Clock::now();
    const auto old_track = state.track_id;
    const auto old_title = state.title;
    const auto old_url = state.media_url;
    const auto old_playback = state.playback;
    const auto old_rate = state.playback_rate;
    state.position = estimated_position(state, now);
    state.sampled_at = now;
    bool got_position = false;
    int result = sd_bus_message_enter_container(message, 'a', "{sv}");
    if (result < 0) return result;
    while ((result = sd_bus_message_enter_container(message, 'e', "sv")) > 0) {
        const char* key = nullptr;
        if ((result = sd_bus_message_read(message, "s", &key)) < 0) return result;
        const char* signature = nullptr;
        if ((result = sd_bus_message_peek_type(message, nullptr, &signature)) < 0) return result;
        if ((result = sd_bus_message_enter_container(message, 'v', signature)) < 0) return result;
        const string_view name = key;
        if (name == "Metadata" && strcmp(signature, "a{sv}") == 0) {
            if ((result = read_metadata(message, player)) < 0) return result;
        } else if (name == "PlaybackStatus" && strcmp(signature, "s") == 0) {
            const char* value = nullptr;
            if ((result = sd_bus_message_read(message, "s", &value)) < 0) return result;
            state.playback = playback(value);
        } else if (name == "Rate" && strcmp(signature, "d") == 0) {
            double value = 1;
            if ((result = sd_bus_message_read(message, "d", &value)) < 0) return result;
            if (isfinite(value) && value != 0 && abs(value) <= 1000) state.playback_rate = value;
        } else if (name == "Position" && strcmp(signature, "x") == 0) {
            int64_t position = 0;
            if ((result = sd_bus_message_read(message, "x", &position)) < 0) return result;
            state.position = Milliseconds{max<int64_t>(position / 1000, 0)};
            got_position = true;
        } else if (strcmp(signature, "b") == 0) {
            int value = 0;
            if ((result = sd_bus_message_read(message, "b", &value)) < 0) return result;
            if (name == "CanControl") state.capabilities.control = value;
            else if (name == "CanPlay") state.capabilities.play = value;
            else if (name == "CanPause") state.capabilities.pause = value;
            else if (name == "CanGoNext") state.capabilities.next = value;
            else if (name == "CanGoPrevious") state.capabilities.previous = value;
        } else if ((result = sd_bus_message_skip(message, signature)) < 0) return result;
        if ((result = sd_bus_message_exit_container(message)) < 0) return result;
        if ((result = sd_bus_message_exit_container(message)) < 0) return result;
    }
    if (result < 0) return result;
    if ((result = sd_bus_message_exit_container(message)) < 0) return result;
    const bool changed_track = old_track != state.track_id || old_title != state.title || old_url != state.media_url;
    if (changed_track) {
        ++player.track_revision;
        if (!got_position) state.position = Milliseconds{};
    }
    player.valid = true;
    state.has_player = true;
    ++player.properties_revision;
    ++revision_;
    if (state.playback == Playback::Playing && (old_playback != Playback::Playing || changed_track || !player.last_active))
        player.last_active = ++activity_;
    if (!got_position && (changed_track || old_playback != state.playback || old_rate != state.playback_rate))
        request_position(state.service);
    return 0;
}

void Mpris::reply(sd_bus_message* message, Pending& pending) {
    pending.done = true;
    auto* player = pending.service.empty() ? nullptr : find(pending.service);
    if (!pending.service.empty() && (!player || player->instance != pending.instance)) return;
    if (player && pending.kind == Call::Properties) player->properties_pending = false;
    if (player && pending.kind == Call::Position) player->position_pending = false;
    if (sd_bus_message_is_method_error(message, nullptr)) {
        if (pending.kind == Call::Control || pending.kind == Call::List) {
            const auto* error = sd_bus_message_get_error(message);
            error_ = bus_error(-sd_bus_message_get_errno(message), error && error->message ? error->message : "D-Bus call failed");
        }
        return;
    }
    int result = 0;
    if (pending.kind == Call::List) {
        result = sd_bus_message_enter_container(message, 'a', "s");
        if (result < 0) { error_ = bus_error(result); return; }
        const char* name = nullptr;
        while ((result = sd_bus_message_read(message, "s", &name)) > 0) discover(name);
    } else if (pending.kind == Call::Owner) {
        const char* owner = nullptr;
        result = sd_bus_message_read(message, "s", &owner);
        if (result > 0 && player->owner.empty()) { player->owner = owner; request_properties(*player); }
    } else if (pending.kind == Call::Properties) {
        if (pending.revision != player->properties_revision) { request_properties(*player); return; }
        result = read_properties(message, *player);
    } else if (pending.kind == Call::Position) {
        if (pending.track != player->track_revision || pending.revision != player->properties_revision) {
            request_position(player->state.service); return;
        }
        result = sd_bus_message_enter_container(message, 'v', "x");
        int64_t value = 0;
        if (result >= 0) result = sd_bus_message_read(message, "x", &value);
        if (result >= 0) {
            player->state.position = Milliseconds{max<int64_t>(0, value / 1000)};
            player->state.sampled_at = Clock::now();
            ++revision_;
        }
    }
    if (result < 0) error_ = bus_error(result);
}

void Mpris::signal(sd_bus_message* message) {
    if (sd_bus_message_is_signal(message, "org.freedesktop.DBus", "NameOwnerChanged")) {
        const char *name = nullptr, *old_owner = nullptr, *new_owner = nullptr;
        const auto result = sd_bus_message_read(message, "sss", &name, &old_owner, &new_owner);
        if (result < 0) { error_ = bus_error(result); return; }
        if (!is_service(name)) return;
        if (*new_owner) discover(name, new_owner);
        else {
            erase_if(players_, [&](const auto& player) { return player.state.service == name; });
            ++revision_;
        }
        return;
    }
    const auto* owner = sd_bus_message_get_sender(message);
    if (!owner) return;
    // Several service aliases may share the same connection/object. Keep each
    // alias current, including one explicitly selected by the caller.
    for (auto& player : players_) {
        if (player.owner != owner) continue;
        sd_bus_message_rewind(message, true);
        player_signal(message, player);
    }
}

void Mpris::player_signal(sd_bus_message* message, Player& selected) {
    auto* player = &selected;
    if (sd_bus_message_is_signal(message, properties_interface, "PropertiesChanged")) {
        const char* interface = nullptr;
        int result = sd_bus_message_read(message, "s", &interface);
        if (result < 0) { error_ = bus_error(result); return; }
        if (strcmp(interface, player_interface) != 0) return;
        result = read_properties(message, *player);
        if (result < 0) { error_ = bus_error(result); return; }
        result = sd_bus_message_enter_container(message, 'a', "s");
        const char* invalidated = nullptr;
        bool refresh = false;
        while (result >= 0 && (result = sd_bus_message_read(message, "s", &invalidated)) > 0) refresh = true;
        if (refresh) request_properties(*player);
    } else if (sd_bus_message_is_signal(message, player_interface, "Seeked")) {
        int64_t position = 0;
        const auto result = sd_bus_message_read(message, "x", &position);
        if (result < 0) { error_ = bus_error(result); return; }
        player->state.position = Milliseconds{max<int64_t>(0, position / 1000)};
        player->state.sampled_at = Clock::now();
        // Invalidate an older Get(Position) response arriving after this signal.
        ++player->properties_revision;
        ++revision_;
    }
}

int Mpris::reply_callback(sd_bus_message* message, void* user, sd_bus_error*) noexcept {
    auto& pending = *static_cast<Pending*>(user);
    try { pending.self->reply(message, pending); }
    catch (...) { pending.done = true; return -ENOMEM; }
    return 0;
}

int Mpris::signal_callback(sd_bus_message* message, void* user, sd_bus_error*) noexcept {
    try { static_cast<Mpris*>(user)->signal(message); }
    catch (...) { return -ENOMEM; }
    return 0;
}

int Mpris::install_callback(sd_bus_message* message, void* user, sd_bus_error*) noexcept {
    if (sd_bus_message_is_method_error(message, nullptr)) {
        try { static_cast<Mpris*>(user)->error_ = bus_error(-sd_bus_message_get_errno(message), "MPRIS subscription failed"); }
        catch (...) { return -ENOMEM; }
    }
    return 0;
}

expected<void, Error> Mpris::process() {
    work_remaining_ = false;
    for (unsigned i = 0; i < 256; ++i) {
        const auto result = sd_bus_process(bus_, nullptr);
        if (result < 0) return unexpected(bus_error(result));
        if (result == 0) break;
        if (i == 255) work_remaining_ = true;
    }
    erase_if(pending_, [](const auto& call) { return call->done; });
    return {};
}

expected<pollfd, Error> Mpris::poll_fd() const {
    const auto fd = sd_bus_get_fd(bus_);
    if (fd < 0) return unexpected(bus_error(fd));
    const auto events = sd_bus_get_events(bus_);
    if (events < 0) return unexpected(bus_error(events));
    return pollfd{fd, static_cast<short>(events), 0};
}

optional<Clock::time_point> Mpris::deadline() const noexcept {
    if (work_remaining_) return Clock::now();
    uint64_t timeout = 0;
    if (sd_bus_get_timeout(bus_, &timeout) < 0 || timeout == numeric_limits<uint64_t>::max()) return {};
    timespec monotonic{};
    if (clock_gettime(CLOCK_MONOTONIC, &monotonic) != 0) return Clock::now();
    const auto elapsed = static_cast<uint64_t>(monotonic.tv_sec) * 1000000 + monotonic.tv_nsec / 1000;
    if (timeout <= elapsed) return Clock::now();
    const auto delta = min<uint64_t>(timeout - elapsed, 86400000000ULL);
    return Clock::now() + chrono::microseconds{delta};
}

optional<Error> Mpris::take_error() {
    auto result = move(error_);
    error_.reset();
    return result;
}

expected<void, Error> Mpris::control(string_view service, const char* method) {
    auto* player = find(service);
    if (!player || !player->valid) return unexpected(Error{ErrorCode::NoPlayer, 0, "No active MPRIS player"});
    const auto result = async(Call::Control, player, player->owner.c_str(), player_path, player_interface, method);
    if (result < 0) return unexpected(bus_error(result));
    return {};
}

} // namespace lyricsmpris::detail
