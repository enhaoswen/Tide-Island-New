#include "mpris.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <time.h>

#include "utility.hpp"

using namespace std;

namespace LyricsMpris::Detail {
namespace {

constexpr auto k_player_path{"/org/mpris/MediaPlayer2"};
constexpr auto k_player_interface{"org.mpris.MediaPlayer2.Player"};
constexpr auto k_properties_interface{"org.freedesktop.DBus.Properties"};

Error bus_error(int code, string_view message = {}) {
	return {ErrorCode::Bus, code, message.empty() ? strerror(code < 0 ? -code : code) : string(message)};
}

string bounded(const char* value, size_t limit = 4096) {
	if(!value)
		return {};
	const auto length{strnlen(value, limit + 1)};
	return length > limit ? string{} : string(value, length);
}

Playback playback(string_view value) {
	if(value == "Playing")
		return Playback::Playing;
	if(value == "Paused")
		return Playback::Paused;
	return Playback::Stopped;
}

}

Milliseconds estimated_position(const State& state, Clock::time_point now) {
	long double value{static_cast<long double>(state.position_.count())};
	if(state.playback_ == Playback::Playing && now > state.sampled_at_) {
		const auto elapsed{chrono::duration<long double, milli>(now - state.sampled_at_).count()};
		value += elapsed * state.playback_rate_;
	}
	const auto maximum{state.duration_ ? state.duration_->count() : numeric_limits<int64_t>::max()};
	value = clamp(value, 0.0L, static_cast<long double>(maximum));
	return Milliseconds{static_cast<int64_t>(value)};
}

struct Mpris::Pending {
	Mpris*       self_{};
	Call         kind_{};
	string       service_{};
	uint64_t     instance_{};
	uint64_t     revision_{};
	uint64_t     track_{};
	sd_bus_slot* slot_{};
	bool         is_done_{};

	~Pending() { sd_bus_slot_unref(slot_); }
};

Mpris::Mpris(const Options& options) :
	blocked_(options.blocked_services_),
	max_lyrics_bytes_(options.max_response_bytes_) {
	matches_.reserve(3);
	players_.reserve(4);
}

expected<unique_ptr<Mpris>, Error> Mpris::create(const Options& options) {
	auto self{unique_ptr<Mpris>(new Mpris(options))};
	int  result{sd_bus_open_user(&self->bus_)};
	if(result < 0)
		return unexpected(bus_error(result));
	sd_bus_set_method_call_timeout(self->bus_, 1000000);
	for(const auto* match :
		{"type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'",
			"type='signal',path='/org/mpris/"
			"MediaPlayer2',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
			"type='signal',path='/org/mpris/MediaPlayer2',interface='org.mpris.MediaPlayer2.Player',member='Seeked'"}) {
		sd_bus_slot* slot{};
		result = sd_bus_add_match_async(self->bus_,
			&slot,
			match,
			&Mpris::signal_callback,
			&Mpris::install_callback,
			self.get());
		if(result < 0)
			return unexpected(bus_error(result));
		self->matches_.push_back(slot);
	}
	result = self->async(Call::List,
		nullptr,
		"org.freedesktop.DBus",
		"/org/freedesktop/DBus",
		"org.freedesktop.DBus",
		"ListNames");
	if(result < 0)
		return unexpected(bus_error(result));
	return self;
}

Mpris::~Mpris() {
	pending_.clear();
	for(auto* slot : matches_)
		sd_bus_slot_unref(slot);
	sd_bus_close_unref(bus_);
}

Player* Mpris::find(string_view service) {
	const auto at = find_if(players_.begin(), players_.end(), [&](const Player& player) {
		return player.state_.service_ == service;
	});
	return at == players_.end() ? nullptr : &*at;
}

bool Mpris::is_blocked(string_view service) const {
	return any_of(blocked_.begin(), blocked_.end(), [&](const auto& fragment) {
		return !fragment.empty() && service.find(fragment) != service.npos;
	});
}

int Mpris::async(Call kind,
	Player*           player,
	const char*       destination,
	const char*       path,
	const char*       interface,
	const char*       method,
	const char*       argument) {
	if(pending_.size() >= 128)
		return -ENOBUFS;
	auto call{make_unique<Pending>()};
	call->self_ = this;
	call->kind_ = kind;
	if(player) {
		call->service_  = player->state_.service_;
		call->instance_ = player->instance_;
		call->revision_ = player->properties_revision_;
		call->track_    = player->track_revision_;
	}
	int result{};
	if(kind == Call::Position) {
		result = sd_bus_call_method_async(bus_,
			&call->slot_,
			destination,
			path,
			interface,
			method,
			&Mpris::reply_callback,
			call.get(),
			"ss",
			k_player_interface,
			"Position");
	} else {
		result = sd_bus_call_method_async(bus_,
			&call->slot_,
			destination,
			path,
			interface,
			method,
			&Mpris::reply_callback,
			call.get(),
			argument ? "s" : "",
			argument);
	}
	if(result >= 0)
		pending_.push_back(std::move(call));
	return result;
}

void Mpris::discover(string_view service, string_view owner) {
	if(!is_service(service) || is_blocked(service))
		return;
	auto* player{find(service)};
	if(!player) {
		if(players_.size() >= 32)
			return;
		players_.emplace_back();
		player                  = &players_.back();
		player->state_.service_ = service;
		player->instance_       = ++next_instance_;
	}
	if(owner.empty()) {
		if(!player->owner_.empty())
			return;
		const auto result = async(Call::Owner,
			player,
			"org.freedesktop.DBus",
			"/org/freedesktop/DBus",
			"org.freedesktop.DBus",
			"GetNameOwner",
			player->state_.service_.c_str());
		if(result < 0)
			error_ = bus_error(result);
	} else {
		if(player->owner_ != owner) {
			const auto name{player->state_.service_};
			*player                 = Player{};
			player->state_.service_ = name;
			player->owner_          = owner;
			player->instance_       = ++next_instance_;
		}
		request_properties(*player);
	}
}

void Mpris::request_properties(Player& player) {
	if(player.are_properties_pending_ || player.owner_.empty())
		return;
	const auto result = async(Call::Properties,
		&player,
		player.owner_.c_str(),
		k_player_path,
		k_properties_interface,
		"GetAll",
		k_player_interface);
	if(result < 0)
		error_ = bus_error(result);
	else
		player.are_properties_pending_ = true;
}

void Mpris::request_position(string_view service) {
	auto* player{find(service)};
	if(!player || !player->is_valid_ || player->is_position_pending_)
		return;
	const auto result{
		async(Call::Position, player, player->owner_.c_str(), k_player_path, k_properties_interface, "Get")};
	if(result < 0)
		error_ = bus_error(result);
	else
		player->is_position_pending_ = true;
}

int Mpris::read_metadata(sd_bus_message* message, Player& player) {
	auto& state{player.state_};
	state.track_id_.clear();
	state.title_.clear();
	state.artists_.clear();
	state.album_.clear();
	state.artwork_url_.clear();
	state.media_url_.clear();
	state.duration_.reset();
	player.inline_lyrics_.clear();
	string comment{};
	int    result{sd_bus_message_enter_container(message, 'a', "{sv}")};
	if(result < 0)
		return result;
	while((result = sd_bus_message_enter_container(message, 'e', "sv")) > 0) {
		const char* key{};
		if((result = sd_bus_message_read(message, "s", &key)) < 0)
			return result;
		const char* signature{};
		if((result = sd_bus_message_peek_type(message, nullptr, &signature)) < 0)
			return result;
		if((result = sd_bus_message_enter_container(message, 'v', signature)) < 0)
			return result;
		const string_view name{key};
		if(strcmp(signature, "s") == 0 || strcmp(signature, "o") == 0) {
			const char* text{};
			if((result = sd_bus_message_read(message, signature, &text)) < 0)
				return result;
			if(name == "mpris:trackid")
				state.track_id_ = bounded(text);
			else if(name == "xesam:title")
				state.title_ = bounded(text);
			else if(name == "xesam:album")
				state.album_ = bounded(text);
			else if(name == "mpris:artUrl")
				state.artwork_url_ = bounded(text, 16384);
			else if(name == "xesam:url")
				state.media_url_ = bounded(text, 16384);
			else if(name == "xesam:asText")
				player.inline_lyrics_ = bounded(text, max_lyrics_bytes_);
			else if(name == "xesam:comment")
				comment = bounded(text, max_lyrics_bytes_);
		} else if(name == "mpris:length" && strcmp(signature, "x") == 0) {
			int64_t length{};
			if((result = sd_bus_message_read(message, "x", &length)) < 0)
				return result;
			if(length > 0)
				state.duration_ = Milliseconds{length / 1000};
		} else if(name == "mpris:length" && strcmp(signature, "t") == 0) {
			// The spec says int64, but Spotify (and others) send uint64.
			uint64_t length{};
			if((result = sd_bus_message_read(message, "t", &length)) < 0)
				return result;
			if(length > 0 && length <= static_cast<uint64_t>(numeric_limits<int64_t>::max()))
				state.duration_ = Milliseconds{static_cast<int64_t>(length / 1000)};
		} else if((name == "xesam:artist" || name == "xesam:comment") && strcmp(signature, "as") == 0) {
			if((result = sd_bus_message_enter_container(message, 'a', "s")) < 0)
				return result;
			const char* item{};
			while((result = sd_bus_message_read(message, "s", &item)) > 0) {
				if(name == "xesam:artist" && state.artists_.size() < 32)
					state.artists_.push_back(bounded(item));
				else if(name == "xesam:comment") {
					const auto line{bounded(item, max_lyrics_bytes_)};
					if(comment.size() + line.size() + 1 <= max_lyrics_bytes_) {
						comment += line;
						comment += '\n';
					}
				}
			}
			if(result < 0)
				return result;
			if((result = sd_bus_message_exit_container(message)) < 0)
				return result;
		} else if((result = sd_bus_message_skip(message, signature)) < 0)
			return result;
		if((result = sd_bus_message_exit_container(message)) < 0)
			return result;
		if((result = sd_bus_message_exit_container(message)) < 0)
			return result;
	}
	if(result < 0)
		return result;
	if(player.inline_lyrics_.empty())
		player.inline_lyrics_ = std::move(comment);
	// Hash large embedded lyrics once per metadata update, not on volume,
	// playback or seek events while the UI is active.
	player.lyrics_hash_ = hash<string_view>{}(player.inline_lyrics_);
	return sd_bus_message_exit_container(message);
}

int Mpris::read_properties(sd_bus_message* message, Player& player) {
	auto&      state{player.state_};
	const auto now{Clock::now()};
	const auto old_track{state.track_id_};
	const auto old_title{state.title_};
	const auto old_url{state.media_url_};
	const auto old_playback{state.playback_};
	const auto old_rate{state.playback_rate_};
	state.position_   = estimated_position(state, now);
	state.sampled_at_ = now;
	bool has_position{};
	int  result{sd_bus_message_enter_container(message, 'a', "{sv}")};
	if(result < 0)
		return result;
	while((result = sd_bus_message_enter_container(message, 'e', "sv")) > 0) {
		const char* key{};
		if((result = sd_bus_message_read(message, "s", &key)) < 0)
			return result;
		const char* signature{};
		if((result = sd_bus_message_peek_type(message, nullptr, &signature)) < 0)
			return result;
		if((result = sd_bus_message_enter_container(message, 'v', signature)) < 0)
			return result;
		const string_view name{key};
		if(name == "Metadata" && strcmp(signature, "a{sv}") == 0) {
			if((result = read_metadata(message, player)) < 0)
				return result;
		} else if(name == "PlaybackStatus" && strcmp(signature, "s") == 0) {
			const char* value{};
			if((result = sd_bus_message_read(message, "s", &value)) < 0)
				return result;
			state.playback_ = playback(value);
		} else if(name == "Rate" && strcmp(signature, "d") == 0) {
			double value{1};
			if((result = sd_bus_message_read(message, "d", &value)) < 0)
				return result;
			if(isfinite(value) && value != 0 && abs(value) <= 1000)
				state.playback_rate_ = value;
		} else if(name == "Position" && strcmp(signature, "x") == 0) {
			int64_t position{};
			if((result = sd_bus_message_read(message, "x", &position)) < 0)
				return result;
			state.position_ = Milliseconds{max<int64_t>(position / 1000, 0)};
			has_position    = true;
		} else if(strcmp(signature, "b") == 0) {
			int value{};
			if((result = sd_bus_message_read(message, "b", &value)) < 0)
				return result;
			if(name == "CanControl")
				state.capabilities_.can_control_ = value;
			else if(name == "CanPlay")
				state.capabilities_.can_play_ = value;
			else if(name == "CanPause")
				state.capabilities_.can_pause_ = value;
			else if(name == "CanGoNext")
				state.capabilities_.can_next_ = value;
			else if(name == "CanGoPrevious")
				state.capabilities_.can_previous_ = value;
		} else if((result = sd_bus_message_skip(message, signature)) < 0)
			return result;
		if((result = sd_bus_message_exit_container(message)) < 0)
			return result;
		if((result = sd_bus_message_exit_container(message)) < 0)
			return result;
	}
	if(result < 0)
		return result;
	if((result = sd_bus_message_exit_container(message)) < 0)
		return result;
	const bool has_track_changed{
		old_track != state.track_id_ || old_title != state.title_ || old_url != state.media_url_};
	if(has_track_changed) {
		++player.track_revision_;
		if(!has_position)
			state.position_ = Milliseconds{};
	}
	player.is_valid_  = true;
	state.has_player_ = true;
	++player.properties_revision_;
	++revision_;
	if(state.playback_ == Playback::Playing &&
		(old_playback != Playback::Playing || has_track_changed || !player.last_active_))
		player.last_active_ = ++activity_;
	if(!has_position && (has_track_changed || old_playback != state.playback_ || old_rate != state.playback_rate_))
		request_position(state.service_);
	return 0;
}

void Mpris::reply(sd_bus_message* message, Pending& pending) {
	pending.is_done_ = true;
	auto* player{pending.service_.empty() ? nullptr : find(pending.service_)};
	if(!pending.service_.empty() && (!player || player->instance_ != pending.instance_))
		return;
	if(player && pending.kind_ == Call::Properties)
		player->are_properties_pending_ = false;
	if(player && pending.kind_ == Call::Position)
		player->is_position_pending_ = false;
	if(sd_bus_message_is_method_error(message, nullptr)) {
		if(pending.kind_ == Call::Control || pending.kind_ == Call::List) {
			const auto* error{sd_bus_message_get_error(message)};
			error_ = bus_error(-sd_bus_message_get_errno(message),
				error && error->message ? error->message : "D-Bus call failed");
		}
		return;
	}
	int result{};
	if(pending.kind_ == Call::List) {
		result = sd_bus_message_enter_container(message, 'a', "s");
		if(result < 0) {
			error_ = bus_error(result);
			return;
		}
		const char* name{};
		while((result = sd_bus_message_read(message, "s", &name)) > 0)
			discover(name);
	} else if(pending.kind_ == Call::Owner) {
		const char* owner{};
		result = sd_bus_message_read(message, "s", &owner);
		if(result > 0 && player->owner_.empty()) {
			player->owner_ = owner;
			request_properties(*player);
		}
	} else if(pending.kind_ == Call::Properties) {
		if(pending.revision_ != player->properties_revision_) {
			request_properties(*player);
			return;
		}
		result = read_properties(message, *player);
	} else if(pending.kind_ == Call::Position) {
		if(pending.track_ != player->track_revision_ || pending.revision_ != player->properties_revision_) {
			request_position(player->state_.service_);
			return;
		}
		result = sd_bus_message_enter_container(message, 'v', "x");
		int64_t value{};
		if(result >= 0)
			result = sd_bus_message_read(message, "x", &value);
		if(result >= 0) {
			player->state_.position_   = Milliseconds{max<int64_t>(0, value / 1000)};
			player->state_.sampled_at_ = Clock::now();
			++revision_;
		}
	}
	if(result < 0)
		error_ = bus_error(result);
}

void Mpris::signal(sd_bus_message* message) {
	if(sd_bus_message_is_signal(message, "org.freedesktop.DBus", "NameOwnerChanged")) {
		const char *name{}, *old_owner{}, *new_owner{};
		const auto  result{sd_bus_message_read(message, "sss", &name, &old_owner, &new_owner)};
		if(result < 0) {
			error_ = bus_error(result);
			return;
		}
		if(!is_service(name))
			return;
		if(*new_owner)
			discover(name, new_owner);
		else {
			erase_if(players_, [&](const auto& player) { return player.state_.service_ == name; });
			++revision_;
		}
		return;
	}
	const auto* owner{sd_bus_message_get_sender(message)};
	if(!owner)
		return;
	// Several service aliases may share the same connection/object. Keep each
	// alias current, including one explicitly selected by the caller.
	for(auto& player : players_) {
		if(player.owner_ != owner)
			continue;
		sd_bus_message_rewind(message, true);
		player_signal(message, player);
	}
}

void Mpris::player_signal(sd_bus_message* message, Player& selected) {
	auto* player{&selected};
	if(sd_bus_message_is_signal(message, k_properties_interface, "PropertiesChanged")) {
		const char* interface{};
		int         result{sd_bus_message_read(message, "s", &interface)};
		if(result < 0) {
			error_ = bus_error(result);
			return;
		}
		if(strcmp(interface, k_player_interface) != 0)
			return;
		result = read_properties(message, *player);
		if(result < 0) {
			error_ = bus_error(result);
			return;
		}
		result = sd_bus_message_enter_container(message, 'a', "s");
		const char* invalidated{};
		bool        should_refresh{};
		while(result >= 0 && (result = sd_bus_message_read(message, "s", &invalidated)) > 0)
			should_refresh = true;
		if(should_refresh)
			request_properties(*player);
	} else if(sd_bus_message_is_signal(message, k_player_interface, "Seeked")) {
		int64_t    position{};
		const auto result{sd_bus_message_read(message, "x", &position)};
		if(result < 0) {
			error_ = bus_error(result);
			return;
		}
		player->state_.position_   = Milliseconds{max<int64_t>(0, position / 1000)};
		player->state_.sampled_at_ = Clock::now();
		// Invalidate an older Get(Position) response arriving after this signal.
		++player->properties_revision_;
		++revision_;
	}
}

int Mpris::reply_callback(sd_bus_message* message, void* user, sd_bus_error*) noexcept {
	auto& pending{*static_cast<Pending*>(user)};
	try {
		pending.self_->reply(message, pending);
	} catch(...) {
		pending.is_done_ = true;
		return -ENOMEM;
	}
	return 0;
}

int Mpris::signal_callback(sd_bus_message* message, void* user, sd_bus_error*) noexcept {
	try {
		static_cast<Mpris*>(user)->signal(message);
	} catch(...) {
		return -ENOMEM;
	}
	return 0;
}

int Mpris::install_callback(sd_bus_message* message, void* user, sd_bus_error*) noexcept {
	if(sd_bus_message_is_method_error(message, nullptr)) {
		try {
			static_cast<Mpris*>(user)->error_ =
				bus_error(-sd_bus_message_get_errno(message), "MPRIS subscription failed");
		} catch(...) {
			return -ENOMEM;
		}
	}
	return 0;
}

expected<void, Error> Mpris::process() {
	has_work_remaining_ = false;
	for(unsigned i{}; i < 256; ++i) {
		const auto result{sd_bus_process(bus_, nullptr)};
		if(result < 0)
			return unexpected(bus_error(result));
		if(result == 0)
			break;
		if(i == 255)
			has_work_remaining_ = true;
	}
	erase_if(pending_, [](const auto& call) { return call->is_done_; });
	return {};
}

expected<pollfd, Error> Mpris::get_poll_fd() const {
	const auto fd{sd_bus_get_fd(bus_)};
	if(fd < 0)
		return unexpected(bus_error(fd));
	const auto events{sd_bus_get_events(bus_)};
	if(events < 0)
		return unexpected(bus_error(events));
	return pollfd{fd, static_cast<short>(events), 0};
}

optional<Clock::time_point> Mpris::get_deadline() const {
	if(has_work_remaining_)
		return Clock::now();
	uint64_t timeout{};
	if(sd_bus_get_timeout(bus_, &timeout) < 0 || timeout == numeric_limits<uint64_t>::max())
		return {};
	timespec monotonic{};
	if(clock_gettime(CLOCK_MONOTONIC, &monotonic) != 0)
		return Clock::now();
	const auto elapsed{
		static_cast<uint64_t>(monotonic.tv_sec) * 1000000 + static_cast<uint64_t>(monotonic.tv_nsec) / 1000};
	if(timeout <= elapsed)
		return Clock::now();
	const auto delta{min<uint64_t>(timeout - elapsed, 86400000000ULL)};
	return Clock::now() + chrono::microseconds{delta};
}

optional<Error> Mpris::take_error() {
	auto result{std::move(error_)};
	error_.reset();
	return result;
}

expected<void, Error> Mpris::control(string_view service, const char* method) {
	auto* player{find(service)};
	if(!player || !player->is_valid_)
		return unexpected(Error{ErrorCode::NoPlayer, 0, "No active MPRIS player"});
	const auto result{async(Call::Control, player, player->owner_.c_str(), k_player_path, k_player_interface, method)};
	if(result < 0)
		return unexpected(bus_error(result));
	return {};
}

}
