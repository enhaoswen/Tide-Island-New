#include "resolver.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utility.hpp"

using namespace std;

namespace LyricsMpris::Detail {
namespace {

string local_path(string_view url) {
	if(url.starts_with("file://")) {
		url.remove_prefix(7);
		if(url.starts_with("localhost/"))
			url.remove_prefix(9);
		if(!url.starts_with('/'))
			return {};
		// A URI fragment/query is not part of a file's name. Encoded ?/# are.
		url = url.substr(0, url.find_first_of("?#"));
		string result{};
		result.reserve(url.size());
		while(!url.empty()) {
			if(url.front() == '%') {
				if(url.size() < 3)
					return {};
				unsigned byte{};
				const auto [end, error] = from_chars(url.data() + 1, url.data() + 3, byte, 16);
				if(error != errc{} || end != url.data() + 3 || byte == 0)
					return {};
				result += static_cast<char>(byte);
				url.remove_prefix(3);
			} else {
				result += url.front();
				url.remove_prefix(1);
			}
		}
		return result;
	}
	if(url.starts_with('/') && url.find('\0') == url.npos)
		return string(url);
	return {};
}

string read_local(const filesystem::path& path, size_t limit) {
	const int fd{open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK)};
	if(fd < 0)
		return {};

	struct Close {
		int fd_{};

		~Close() { close(fd_); }
	} guard{fd};
	struct stat stat{};
	if(fstat(fd, &stat) < 0 || !S_ISREG(stat.st_mode) || stat.st_size <= 0 ||
		static_cast<uint64_t>(stat.st_size) > limit)
		return {};
	string result{};
	result.reserve(static_cast<size_t>(stat.st_size));
	array<char, 4096> buffer{};
	while(true) {
		const auto count{read(fd, buffer.data(), buffer.size())};
		if(count < 0) {
			if(errno == EINTR)
				continue;
			return {};
		}
		if(count == 0)
			return result;
		if(static_cast<size_t>(count) > limit - result.size())
			return {};
		result.append(buffer.data(), static_cast<size_t>(count));
	}
}

bool is_search_stage(Stage stage) {
	return stage == Stage::NeteaseSearch || stage == Stage::QqSearch || stage == Stage::KugouSearch;
}

}

expected<unique_ptr<Resolver>, Error> Resolver::create(const Options& options) {
	// Http (curl + TLS, ~1 MiB once a connection exists) is created per search
	// and released when the search ends; see ensure_http / release_idle_http.
	return unique_ptr<Resolver>(new Resolver(options));
}

bool Resolver::ensure_http() {
	if(http_)
		return true;
	if(options_.providers_.empty())
		return false;
	auto http{Http::create(options_)};
	if(!http) {
		error_              = std::move(http.error());
		has_error_occurred_ = true;
		return false;
	}
	http_ = std::move(*http);
	return true;
}

void Resolver::release_idle_http() {
	if(state_ != SearchState::Searching)
		http_.reset();
}

void Resolver::set_document(Lyrics document) {
	document_ = std::move(document);
	++document_revision_;
}

void Resolver::clear() {
	http_.reset();
	jobs_.clear();
	query_ = {};
	set_document({});
	best_synced_       = {};
	best_synced_score_ = best_plain_score_ = -1;
	followup_.reset();
	retry_at_.reset();
	attempts_ = 0;
	state_    = SearchState::Idle;
	error_.reset();
	has_error_occurred_ = false;
}

bool Resolver::direct(string_view text, string_view provider) {
	auto document{parse_lyrics(text, provider, {options_.max_response_bytes_, options_.max_lyric_lines_})};
	if(!document || document->is_empty())
		return false;
	if(document->is_synced() || document->is_instrumental()) {
		set_document(std::move(*document));
		state_ = SearchState::Finished;
		return true;
	}
	const int score{provider == "mpris" ? 20000 : 10000};
	if(!document_.is_synced() && score >= best_plain_score_) {
		set_document(std::move(*document));
		best_plain_score_ = score;
	}
	return false;
}

void Resolver::local(string_view media_url) {
	const auto path{local_path(media_url)};
	if(path.empty())
		return;
	const filesystem::path media(path);
	const auto             base{media.stem().native()};
	const auto             directory{media.parent_path()};
	for(const auto& candidate : {directory / (base + ".lrc"),
			directory / (base + ".LRC"),
			filesystem::path(path + ".lrc"),
			directory / "lyrics" / (base + ".lrc")}) {
		const auto text{read_local(candidate, options_.max_response_bytes_)};
		if(!text.empty() && direct(text, "local"))
			return;
	}
}

void Resolver::start(TrackQuery query, string_view inline_lyrics, string_view media_url, bool is_fresh) {
	if(is_fresh)
		clear();
	else {
		if(http_)
			http_->cancel();
		jobs_.clear();
		best_synced_       = {};
		best_synced_score_ = -1;
		retry_at_.reset();
	}
	query_ = std::move(query);
	++attempts_;
	state_                   = SearchState::Searching;
	has_successful_response_ = false;
	has_error_occurred_      = false;
	provider_index_          = 0;
	followup_.reset();
	if(direct(inline_lyrics, "mpris"))
		return;
	if(options_.should_use_local_lyrics_)
		local(media_url);
	if(state_ == SearchState::Finished)
		return;
	if(trim(query_.title_).empty() || !ensure_http())
		complete();
	else
		next_provider();
	release_idle_http();
}

void Resolver::schedule(Stage stage, Candidate candidate) {
	if(!http_)
		return;
	const auto id{++next_id_};
	auto       request{provider_request(id, stage, query_, candidate, options_.musixmatch_api_key_)};
	if(http_->enqueue(std::move(request)))
		jobs_.emplace(id, Job{stage, std::move(candidate)});
	else
		error_ = Error{ErrorCode::Network, 0, "Lyrics request queue limit reached"};
}

void Resolver::next_provider() {
	while(provider_index_ < options_.providers_.size()) {
		const auto provider{options_.providers_[provider_index_++]};
		Candidate  metadata{};
		metadata.provider_ = provider;
		followup_.reset();
		switch(provider) {
			case Provider::Lrclib:
				followup_ = Stage::LrclibSearch;
				schedule(Stage::LrclibGet, std::move(metadata));
				break;
			case Provider::Lrcx:
				followup_ = Stage::LrcxText;
				schedule(Stage::LrcxJson, std::move(metadata));
				break;
			case Provider::Netease:
				schedule(Stage::NeteaseSearch, std::move(metadata));
				break;
			case Provider::Qq:
				schedule(Stage::QqSearch, std::move(metadata));
				break;
			case Provider::Kugou:
				schedule(Stage::KugouSearch, std::move(metadata));
				break;
			case Provider::Musixmatch:
				if(options_.musixmatch_api_key_.empty())
					continue;
				metadata.title_    = query_.title_;
				metadata.artist_   = query_.artist_;
				metadata.album_    = query_.album_;
				metadata.duration_ = query_.duration_;
				followup_          = Stage::MusixmatchPlain;
				schedule(Stage::MusixmatchSubtitle, std::move(metadata));
				break;
		}
		if(!jobs_.empty())
			return;
	}
	complete();
}

void Resolver::complete() {
	followup_.reset();
	if(!best_synced_.is_empty()) {
		set_document(std::move(best_synced_));
		state_ = SearchState::Finished;
	} else if(!document_.is_empty())
		state_ = SearchState::Finished;
	else if(attempts_ < options_.max_search_attempts_) {
		int64_t delay{options_.retry_delay_.count()};
		for(unsigned i{1}; i < attempts_; ++i)
			delay = min<int64_t>(delay * 3, 86400000);
		retry_at_ = Clock::now() + Milliseconds{delay};
		state_    = SearchState::Retrying;
	} else
		state_ = has_successful_response_ || !has_error_occurred_ ? SearchState::NotFound : SearchState::Failed;
}

void Resolver::consider(Candidate candidate) {
	const auto evaluation{evaluate(query_, candidate)};
	if(!evaluation.is_accepted_)
		return;
	auto parsed = parse_lyrics(candidate.synced_.empty() ? candidate.plain_ : candidate.synced_,
		provider_name(candidate.provider_),
		{options_.max_response_bytes_, options_.max_lyric_lines_});
	if(!parsed)
		return;
	if(candidate.is_instrumental_)
		DetailLyricsAccess::set_instrumental(*parsed);
	if(parsed->is_empty())
		return;
	if(parsed->is_synced() || parsed->is_instrumental()) {
		if(evaluation.is_high_confidence_) {
			set_document(std::move(*parsed));
			http_->cancel();
			jobs_.clear();
			followup_.reset();
			state_ = SearchState::Finished;
		} else if(evaluation.score_ > best_synced_score_) {
			best_synced_score_ = evaluation.score_;
			best_synced_       = std::move(*parsed);
		}
	} else if(evaluation.score_ > best_plain_score_) {
		best_plain_score_ = evaluation.score_;
		set_document(std::move(*parsed));
	}
}

void Resolver::receive(HttpResponse response) {
	const auto at{jobs_.find(response.id_)};
	if(at == jobs_.end())
		return; // Canceled track or an earlier winning response.
	auto job{std::move(at->second)};
	jobs_.erase(at);
	if(response.error_ != CURLE_OK) {
		has_error_occurred_ = true;
		error_ = Error{ErrorCode::Network, static_cast<int>(response.error_), curl_easy_strerror(response.error_)};
		return;
	}
	if(response.status_ < 200 || response.status_ >= 300) {
		if(response.status_ == 404)
			has_successful_response_ = true;
		else {
			has_error_occurred_ = true;
			error_              = Error{ErrorCode::Network,
				static_cast<int>(response.status_),
				"Lyrics provider returned HTTP " + to_string(response.status_)};
		}
		return;
	}
	auto candidates{parse_candidates(response.body_, job.metadata_.provider_, job.stage_)};
	if(!candidates) {
		has_error_occurred_ = true;
		error_              = Error{ErrorCode::Network, 0, std::move(candidates.error())};
		return;
	}
	has_successful_response_ = true;
	if(is_search_stage(job.stage_)) {
		struct Rated {
			int    score_{};
			size_t index_{};
		};

		vector<Rated> rated{};
		for(size_t i{}; i < candidates->size(); ++i) {
			const auto evaluation{evaluate(query_, (*candidates)[i])};
			if(evaluation.is_accepted_)
				rated.push_back({evaluation.score_, i});
		}
		stable_sort(rated.begin(), rated.end(), [](auto a, auto b) { return a.score_ > b.score_; });
		const auto stage = job.stage_ == Stage::NeteaseSearch ? Stage::NeteaseLyric
			: job.stage_ == Stage::QqSearch                   ? Stage::QqLyric
															  : Stage::KugouLyricSearch;
		for(size_t i{}; i < min(rated.size(), size_t{5}); ++i)
			schedule(stage, std::move((*candidates)[rated[i].index_]));
	} else if(job.stage_ == Stage::KugouLyricSearch) {
		if(!candidates->empty()) {
			auto candidate{std::move(job.metadata_)};
			candidate.resource_id_ = candidates->front().resource_id_;
			candidate.access_key_  = candidates->front().access_key_;
			if(!candidate.resource_id_.empty() && !candidate.access_key_.empty())
				schedule(Stage::KugouDownload, std::move(candidate));
		}
	} else {
		for(auto& candidate : *candidates) {
			if(candidate.title_.empty())
				candidate.title_ = job.metadata_.title_;
			if(candidate.artist_.empty())
				candidate.artist_ = job.metadata_.artist_;
			if(candidate.album_.empty())
				candidate.album_ = job.metadata_.album_;
			if(!candidate.duration_)
				candidate.duration_ = job.metadata_.duration_;
			consider(std::move(candidate));
			if(state_ == SearchState::Finished)
				break;
		}
	}
}

expected<void, Error> Resolver::process(span<const pollfd> ready, Clock::time_point now) {
	if(!http_ || state_ != SearchState::Searching)
		return {};
	auto responses{http_->process(ready, now)};
	if(!responses) {
		http_->cancel();
		jobs_.clear();
		followup_.reset();
		error_              = responses.error();
		has_error_occurred_ = true;
		complete();
		release_idle_http();
		return {};
	}
	for(auto& response : *responses)
		receive(std::move(response));
	if(state_ == SearchState::Searching && jobs_.empty() && !http_->is_busy()) {
		if(followup_) {
			const auto stage{*followup_};
			followup_.reset();
			Candidate candidate{};
			candidate.provider_ = options_.providers_[provider_index_ - 1];
			if(candidate.provider_ == Provider::Musixmatch) {
				candidate.title_    = query_.title_;
				candidate.artist_   = query_.artist_;
				candidate.album_    = query_.album_;
				candidate.duration_ = query_.duration_;
			}
			schedule(stage, std::move(candidate));
		} else
			next_provider();
	}
	release_idle_http();
	return {};
}

span<const pollfd> Resolver::get_poll_fds() const {
	return http_ ? http_->get_poll_fds() : span<const pollfd>{};
}

optional<Clock::time_point> Resolver::get_deadline() const {
	auto deadline{http_ ? http_->get_deadline() : nullopt};
	if(retry_at_ && (!deadline || *retry_at_ < *deadline))
		deadline = retry_at_;
	return deadline;
}

optional<Error> Resolver::take_error() {
	auto error{std::move(error_)};
	error_.reset();
	return error;
}

}
