#include "client.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

#include "detail/mpris.hpp"
#include "detail/resolver.hpp"
#include "detail/utility.hpp"

using namespace std;

namespace LyricsMpris {
namespace {

void mark(Changes& changes, Changes flag) {
	changes = changes | flag;
}

bool are_options_valid(const Options& options) {
	return options.max_response_bytes_ > 0 && options.max_response_bytes_ <= 8 * 1024 * 1024 &&
		options.max_lyric_lines_ > 0 && options.max_lyric_lines_ <= 100000 && options.max_connections_ > 0 &&
		options.max_connections_ <= 8 && options.request_timeout_ > Milliseconds{} &&
		options.request_timeout_ <= Milliseconds{600000} && options.max_search_attempts_ > 0 &&
		options.max_search_attempts_ <= 10 && options.retry_delay_ > Milliseconds{} &&
		options.retry_delay_ <= Milliseconds{86400000} && options.position_resync_interval_ >= Milliseconds{} &&
		options.position_resync_interval_ <= Milliseconds{86400000} &&
		options.lyric_offset_ >= Milliseconds{-3600000} && options.lyric_offset_ <= Milliseconds{3600000} &&
		options.providers_.size() <= 6 &&
		all_of(options.providers_.begin(), options.providers_.end(), [](auto provider) {
			return provider >= Provider::Lrclib && provider <= Provider::Musixmatch;
		});
}

bool is_metadata_equal(const State& a, const State& b) {
	return tie(a.has_player_,
			   a.service_,
			   a.track_id_,
			   a.title_,
			   a.artists_,
			   a.album_,
			   a.artwork_url_,
			   a.media_url_,
			   a.duration_) ==
		tie(b.has_player_,
			b.service_,
			b.track_id_,
			b.title_,
			b.artists_,
			b.album_,
			b.artwork_url_,
			b.media_url_,
			b.duration_);
}

bool are_capabilities_equal(const Capabilities& a, const Capabilities& b) {
	return tie(a.can_control_, a.can_play_, a.can_pause_, a.can_next_, a.can_previous_) ==
		tie(b.can_control_, b.can_play_, b.can_pause_, b.can_next_, b.can_previous_);
}

Detail::TrackQuery query_for(const State& state) {
	Detail::TrackQuery query{};
	query.title_    = state.title_;
	query.album_    = state.album_;
	query.duration_ = state.duration_;
	for(const auto& artist : state.artists_) {
		if(!query.artist_.empty())
			query.artist_ += ", ";
		query.artist_ += artist;
	}
	return query;
}

}

struct Client::Impl {
	Options                      options_{};
	unique_ptr<Detail::Mpris>    mpris_{};
	unique_ptr<Detail::Resolver> resolver_{};
	State                        state_{};
	vector<pollfd>               fds_{};
	uint64_t                     bus_revision_{numeric_limits<uint64_t>::max()};
	uint64_t                     document_revision_{};
	uint64_t                     player_instance_{};
	size_t                       inline_hash_{};
	bool                         is_selection_dirty_{true};
	bool                         is_retry_requested_{};
	bool                         is_offset_dirty_{};
	optional<Clock::time_point>  resync_at_{};
	optional<Error>              poll_error_{};

	explicit Impl(Options value) :
		options_(std::move(value)) {}

	const Detail::Player* choose() const {
		const auto            players{mpris_->get_players()};
		const Detail::Player* current{};
		for(const auto& player : players) {
			if(!player.is_valid_)
				continue;
			if(!options_.preferred_service_.empty() && player.state_.service_ == options_.preferred_service_)
				return &player;
			if(player.state_.service_ == state_.service_ && player.instance_ == player_instance_)
				current = &player;
		}
		// Keep an already selected playing player stable; choose the most
		// recently started player when the selected one stops/pauses/disappears.
		if(current && current->state_.playback_ == Playback::Playing)
			return current;
		const Detail::Player* best{};
		constexpr auto        k_rank = [](const Detail::Player& player) {
			return player.state_.playback_ == Playback::Playing ? 2
				: player.state_.playback_ == Playback::Paused   ? 1
																: 0;
		};
		for(const auto& player : players) {
			if(!player.is_valid_)
				continue;
			if(!best || k_rank(player) > k_rank(*best) ||
				(k_rank(player) == k_rank(*best) && player.last_active_ > best->last_active_) ||
				(k_rank(player) == k_rank(*best) && player.last_active_ == best->last_active_ &&
					player.state_.service_ < best->state_.service_))
				best = &player;
		}
		if(current && best && k_rank(*current) == k_rank(*best) && current->state_.playback_ == Playback::Paused)
			return current;
		return best;
	}

	void refresh(Changes& changes, bool should_force_retry) {
		const auto* player{choose()};
		if(!player) {
			if(state_.has_player_) {
				const auto revision{state_.track_revision_ + 1};
				state_                 = {};
				state_.track_revision_ = revision;
				resolver_->clear();
				player_instance_ = 0;
				inline_hash_     = 0;
				mark(changes, Changes::Player | Changes::Track | Changes::Playback | Changes::Position);
			}
			resync_at_.reset();
			return;
		}
		const auto& incoming{player->state_};
		const bool  is_new_player{player_instance_ != player->instance_ || state_.service_ != incoming.service_};
		const bool  is_new_track{is_new_player || state_.track_id_ != incoming.track_id_ ||
			state_.title_ != incoming.title_ || state_.artists_ != incoming.artists_};
		const bool  is_query_changed{state_.album_ != incoming.album_ || state_.duration_ != incoming.duration_};
		const bool  is_late_url{state_.media_url_ != incoming.media_url_ && !incoming.media_url_.empty()};
		const auto  hash{player->lyrics_hash_};
		const bool  are_lyrics_late{hash != inline_hash_ && !player->inline_lyrics_.empty()};
		const bool  has_playback_changed{
			state_.playback_ != incoming.playback_ || state_.playback_rate_ != incoming.playback_rate_};
		if(is_new_player)
			mark(changes, Changes::Player);
		if(!is_metadata_equal(state_, incoming))
			mark(changes, Changes::Track);
		if(has_playback_changed)
			mark(changes, Changes::Playback);
		if(!are_capabilities_equal(state_.capabilities_, incoming.capabilities_))
			mark(changes, Changes::Capabilities);
		if(state_.position_ != incoming.position_ || state_.sampled_at_ != incoming.sampled_at_)
			mark(changes, Changes::Position);
		const auto revision{state_.track_revision_ + (is_new_track ? 1 : 0)};
		const auto search{state_.lyrics_search_};
		const auto line{state_.current_line_};
		auto       error{std::move(state_.last_error_)};
		state_                 = incoming;
		state_.track_revision_ = revision;
		state_.lyrics_search_  = search;
		state_.current_line_   = line;
		state_.last_error_     = std::move(error);
		player_instance_       = player->instance_;
		inline_hash_           = hash;
		const bool needs_query_restart{is_query_changed && !resolver_->get_document().is_synced()};
		if(is_new_track || should_force_retry || are_lyrics_late || needs_query_restart ||
			(!resolver_->get_document().is_synced() && is_late_url)) {
			state_.last_error_.reset();
			resolver_->start(query_for(state_),
				player->inline_lyrics_,
				state_.media_url_,
				is_new_track || needs_query_restart);
		}
		if(options_.position_resync_interval_ > Milliseconds{} && state_.playback_ == Playback::Playing) {
			if(!resync_at_ || is_new_player || has_playback_changed)
				resync_at_ = Clock::now() + options_.position_resync_interval_;
		} else
			resync_at_.reset();
	}

	expected<void, Error> command(const char* method) {
		const auto* player{choose()};
		if(!player)
			return unexpected(Error{ErrorCode::NoPlayer, 0, "No active MPRIS player"});
		const auto&       capabilities{player->state_.capabilities_};
		bool              is_supported{capabilities.can_control_};
		const string_view operation{method};
		if(operation == "Play")
			is_supported &= capabilities.can_play_;
		else if(operation == "Pause")
			is_supported &= capabilities.can_pause_;
		else if(operation == "Next")
			is_supported &= capabilities.can_next_;
		else if(operation == "Previous")
			is_supported &= capabilities.can_previous_;
		else if(operation == "PlayPause")
			is_supported &=
				player->state_.playback_ == Playback::Playing ? capabilities.can_pause_ : capabilities.can_play_;
		if(!is_supported)
			return unexpected(Error{ErrorCode::Unsupported, 0, "Player does not support " + string(operation)});
		return mpris_->control(player->state_.service_, method);
	}
};

Client::Client(unique_ptr<Impl> impl) :
	impl_(std::move(impl)) {
}

Client::~Client()                            = default;
Client::Client(Client&&) noexcept            = default;
Client& Client::operator=(Client&&) noexcept = default;

expected<Client, Error> Client::create(Options options) {
	if(!are_options_valid(options))
		return unexpected(Error{ErrorCode::InvalidOptions, 0, "Invalid lyricsmpris options or resource limits"});
	// Duplicate providers do not trigger repeated requests.
	vector<Provider> providers{};
	for(const auto provider : options.providers_)
		if(find(providers.begin(), providers.end(), provider) == providers.end())
			providers.push_back(provider);
	options.providers_ = std::move(providers);
	auto impl{make_unique<Impl>(std::move(options))};
	auto mpris{Detail::Mpris::create(impl->options_)};
	if(!mpris)
		return unexpected(std::move(mpris.error()));
	impl->mpris_ = std::move(*mpris);
	auto resolver{Detail::Resolver::create(impl->options_)};
	if(!resolver)
		return unexpected(std::move(resolver.error()));
	impl->resolver_ = std::move(*resolver);
	impl->fds_.reserve(1 + impl->options_.max_connections_ * 2);
	return Client(std::move(impl));
}

const State& Client::get_state() const {
	return impl_->state_;
}

const Lyrics& Client::get_lyrics() const {
	return impl_->resolver_->get_document();
}

Milliseconds Client::position(Clock::time_point now) const {
	return Detail::estimated_position(impl_->state_, now);
}

string_view Client::get_current_lyric() const {
	const auto index{impl_->state_.current_line_};
	return index ? get_lyrics().line(*index).text_ : string_view{};
}

span<const pollfd> Client::get_poll_fds() {
	impl_->fds_.clear();
	if(auto fd = impl_->mpris_->get_poll_fd())
		impl_->fds_.push_back(*fd);
	else
		impl_->poll_error_ = std::move(fd.error());
	const auto http_fds{impl_->resolver_->get_poll_fds()};
	impl_->fds_.insert(impl_->fds_.end(), http_fds.begin(), http_fds.end());
	return impl_->fds_;
}

optional<Clock::time_point> Client::get_next_deadline() const {
	const auto now{Clock::now()};
	if(impl_->is_selection_dirty_ || impl_->is_retry_requested_ || impl_->is_offset_dirty_ || impl_->poll_error_)
		return now;
	auto       deadline{impl_->mpris_->get_deadline()};
	const auto include = [&](optional<Clock::time_point> value) {
		if(value && (!deadline || *value < *deadline))
			deadline = value;
	};
	include(impl_->resolver_->get_deadline());
	include(impl_->resync_at_);
	const auto& state{impl_->state_};
	const auto& document{get_lyrics()};
	const auto  progress{position(now)};
	if(state.playback_ == Playback::Playing && document.is_synced() && document.size() &&
		!(state.playback_rate_ > 0 && state.duration_ && progress >= *state.duration_) &&
		!(state.playback_rate_ < 0 && progress <= Milliseconds{})) {
		const auto             lyric_position{progress - impl_->options_.lyric_offset_};
		const auto             current{document.line_at(lyric_position)};
		optional<Milliseconds> boundary{};
		if(state.playback_rate_ > 0) {
			const auto next{current ? *current + 1 : 0};
			if(next < document.size())
				boundary = document.line(next).start_time_;
		} else if(current)
			boundary = *document.line(*current).start_time_ - Milliseconds{1};
		if(boundary) {
			const auto delay{
				(static_cast<long double>(boundary->count()) - lyric_position.count()) / state.playback_rate_};
			// Ceiling prevents a busy loop caused by sub-millisecond rounding.
			if(delay >= 0 && delay <= 86400000) {
				const auto due{now + Milliseconds{max<int64_t>(1, static_cast<int64_t>(ceil(delay)))}};
				if(!state.duration_ || state.playback_rate_ < 0 ||
					static_cast<long double>(boundary->count()) + impl_->options_.lyric_offset_.count() <=
						state.duration_->count())
					include(due);
			}
		}
	}
	return deadline;
}

void Client::resync_position() {
	if(impl_->state_.has_player_)
		impl_->mpris_->request_position(impl_->state_.service_);
}

expected<Changes, Error> Client::process(span<const pollfd> ready, Clock::time_point now) {
	auto& self{*impl_};
	if(self.poll_error_)
		return unexpected(*self.poll_error_);
	Changes changes{Changes::None};
	if(auto result = self.mpris_->process(); !result)
		return unexpected(std::move(result.error()));
	const bool should_retry{self.is_retry_requested_ || self.resolver_->is_retry_due(now)};
	if(self.is_retry_requested_)
		self.resolver_->reset_attempts();
	if(self.is_selection_dirty_ || should_retry || self.bus_revision_ != self.mpris_->get_revision()) {
		self.refresh(changes, should_retry);
		self.bus_revision_       = self.mpris_->get_revision();
		self.is_selection_dirty_ = false;
		self.is_retry_requested_ = false;
	}
	if(self.resync_at_ && now >= *self.resync_at_) {
		self.mpris_->request_position(self.state_.service_);
		self.resync_at_ = now + self.options_.position_resync_interval_;
	}
	if(auto result = self.resolver_->process(ready, now); !result)
		return unexpected(std::move(result.error()));
	if(self.document_revision_ != self.resolver_->get_document_revision()) {
		self.document_revision_ = self.resolver_->get_document_revision();
		mark(changes, Changes::Lyrics | Changes::Line);
	}
	if(self.state_.lyrics_search_ != self.resolver_->get_state()) {
		self.state_.lyrics_search_ = self.resolver_->get_state();
		mark(changes, Changes::Search);
	}
	const auto next_line{get_lyrics().line_at(position(now) - self.options_.lyric_offset_)};
	if(next_line != self.state_.current_line_ || self.is_offset_dirty_) {
		self.state_.current_line_ = next_line;
		self.is_offset_dirty_     = false;
		mark(changes, Changes::Line);
	}
	for(auto error : {self.mpris_->take_error(), self.resolver_->take_error()}) {
		if(error) {
			self.state_.last_error_ = std::move(error);
			mark(changes, Changes::Error);
		}
	}
	return changes;
}

expected<void, Error> Client::select_player(string_view service) {
	if(!service.empty()) {
		if(!Detail::is_service(service))
			return unexpected(Error{ErrorCode::InvalidOptions, 0, "Expected a full MPRIS service name"});
		const auto players{impl_->mpris_->get_players()};
		if(none_of(players.begin(), players.end(), [&](const auto& player) {
			   return player.is_valid_ && player.state_.service_ == service;
		   }))
			return unexpected(Error{ErrorCode::NoPlayer, 0, "Requested player is unavailable"});
	}
	impl_->options_.preferred_service_ = service;
	impl_->is_selection_dirty_         = true;
	return {};
}

void Client::retry_lyrics() {
	impl_->is_retry_requested_ = true;
}

void Client::set_lyric_offset(Milliseconds offset) {
	impl_->options_.lyric_offset_ = clamp(offset, Milliseconds{-3600000}, Milliseconds{3600000});
	impl_->is_offset_dirty_       = true;
}

expected<void, Error> Client::play() {
	return impl_->command("Play");
}

expected<void, Error> Client::pause() {
	return impl_->command("Pause");
}

expected<void, Error> Client::play_pause() {
	return impl_->command("PlayPause");
}

expected<void, Error> Client::next() {
	return impl_->command("Next");
}

expected<void, Error> Client::previous() {
	return impl_->command("Previous");
}

}
