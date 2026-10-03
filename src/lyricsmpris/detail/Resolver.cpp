#include "Resolver.hpp"
#include "Utility.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

namespace lyricsmpris::detail {
namespace {

std::string local_path(std::string_view url) {
    if (url.starts_with("file://")) {
        url.remove_prefix(7);
        if (url.starts_with("localhost/")) url.remove_prefix(9);
        if (!url.starts_with('/')) return {};
        // A URI fragment/query is not part of a file's name. Encoded ?/# are.
        url = url.substr(0, url.find_first_of("?#"));
        std::string result;
        result.reserve(url.size());
        while (!url.empty()) {
            if (url.front() == '%') {
                if (url.size() < 3) return {};
                unsigned byte = 0;
                const auto [end, error] = std::from_chars(url.data() + 1, url.data() + 3, byte, 16);
                if (error != std::errc{} || end != url.data() + 3 || byte == 0) return {};
                result += static_cast<char>(byte);
                url.remove_prefix(3);
            } else { result += url.front(); url.remove_prefix(1); }
        }
        return result;
    }
    if (url.starts_with('/') && url.find('\0') == url.npos) return std::string(url);
    return {};
}

std::string read_local(const std::filesystem::path& path, std::size_t limit) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return {};
    struct Close { int fd; ~Close() { close(fd); } } guard{fd};
    struct stat stat{};
    if (fstat(fd, &stat) < 0 || !S_ISREG(stat.st_mode) || stat.st_size <= 0
        || static_cast<std::uint64_t>(stat.st_size) > limit) return {};
    std::string result;
    result.reserve(static_cast<std::size_t>(stat.st_size));
    std::array<char, 4096> buffer;
    while (true) {
        const auto count = read(fd, buffer.data(), buffer.size());
        if (count < 0) { if (errno == EINTR) continue; return {}; }
        if (count == 0) return result;
        if (static_cast<std::size_t>(count) > limit - result.size()) return {};
        result.append(buffer.data(), static_cast<std::size_t>(count));
    }
}

bool search_stage(Stage stage) noexcept {
    return stage == Stage::NeteaseSearch || stage == Stage::QqSearch || stage == Stage::KugouSearch;
}

} // namespace

std::expected<std::unique_ptr<Resolver>, Error> Resolver::create(const Options& options) {
    auto resolver = std::unique_ptr<Resolver>(new Resolver(options));
    if (!options.providers.empty()) {
        auto http = Http::create(options);
        if (!http) return std::unexpected(std::move(http.error()));
        resolver->http_ = std::move(*http);
    }
    return resolver;
}

void Resolver::set_document(Lyrics document) {
    document_ = std::move(document);
    ++document_revision_;
}

void Resolver::clear() {
    if (http_) http_->cancel();
    jobs_.clear();
    query_ = {};
    set_document({});
    best_synced_ = {};
    best_synced_score_ = best_plain_score_ = -1;
    followup_.reset(); retry_at_.reset();
    attempts_ = 0;
    state_ = SearchState::Idle;
    error_.reset();
    had_error_ = false;
}

bool Resolver::direct(std::string_view text, std::string_view provider) {
    auto document = parse_lyrics(text, provider, {options_.max_response_bytes, options_.max_lyric_lines});
    if (!document || document->empty()) return false;
    if (document->synced() || document->instrumental()) {
        set_document(std::move(*document));
        state_ = SearchState::Finished;
        return true;
    }
    const int score = provider == "mpris" ? 20000 : 10000;
    if (!document_.synced() && score >= best_plain_score_) {
        set_document(std::move(*document));
        best_plain_score_ = score;
    }
    return false;
}

void Resolver::local(std::string_view media_url) {
    const auto path = local_path(media_url);
    if (path.empty()) return;
    const std::filesystem::path media(path);
    const auto base = media.stem().native();
    const auto directory = media.parent_path();
    for (const auto& candidate : {directory / (base + ".lrc"), directory / (base + ".LRC"),
        std::filesystem::path(path + ".lrc"), directory / "lyrics" / (base + ".lrc")}) {
        auto text = read_local(candidate, options_.max_response_bytes);
        if (!text.empty() && direct(text, "local")) return;
    }
}

void Resolver::start(TrackQuery query, std::string_view inline_lyrics, std::string_view media_url, bool fresh) {
    if (fresh) clear();
    else {
        if (http_) http_->cancel();
        jobs_.clear();
        best_synced_ = {};
        best_synced_score_ = -1;
        retry_at_.reset();
    }
    query_ = std::move(query);
    ++attempts_;
    state_ = SearchState::Searching;
    successful_response_ = false;
    had_error_ = false;
    provider_index_ = 0;
    followup_.reset();
    if (direct(inline_lyrics, "mpris")) return;
    if (options_.local_lyrics) local(media_url);
    if (state_ == SearchState::Finished) return;
    if (trim(query_.title).empty() || !http_) complete();
    else next_provider();
}

void Resolver::schedule(Stage stage, Candidate candidate) {
    if (!http_) return;
    const auto id = ++next_id_;
    auto request = provider_request(id, stage, query_, candidate, options_.musixmatch_api_key);
    if (http_->enqueue(std::move(request))) jobs_.emplace(id, Job{stage, std::move(candidate)});
    else error_ = Error{ErrorCode::Network, 0, "Lyrics request queue limit reached"};
}

void Resolver::next_provider() {
    while (provider_index_ < options_.providers.size()) {
        const auto provider = options_.providers[provider_index_++];
        Candidate metadata;
        metadata.provider = provider;
        followup_.reset();
        switch (provider) {
        case Provider::Lrclib: followup_ = Stage::LrclibSearch; schedule(Stage::LrclibGet, std::move(metadata)); break;
        case Provider::Lrcx: followup_ = Stage::LrcxText; schedule(Stage::LrcxJson, std::move(metadata)); break;
        case Provider::Netease: schedule(Stage::NeteaseSearch, std::move(metadata)); break;
        case Provider::Qq: schedule(Stage::QqSearch, std::move(metadata)); break;
        case Provider::Kugou: schedule(Stage::KugouSearch, std::move(metadata)); break;
        case Provider::Musixmatch:
            if (options_.musixmatch_api_key.empty()) continue;
            metadata.title = query_.title; metadata.artist = query_.artist;
            metadata.album = query_.album; metadata.duration = query_.duration;
            followup_ = Stage::MusixmatchPlain;
            schedule(Stage::MusixmatchSubtitle, std::move(metadata));
            break;
        }
        if (!jobs_.empty()) return;
    }
    complete();
}

void Resolver::complete() {
    followup_.reset();
    if (!best_synced_.empty()) {
        set_document(std::move(best_synced_));
        state_ = SearchState::Finished;
    } else if (!document_.empty()) state_ = SearchState::Finished;
    else if (attempts_ < options_.max_search_attempts) {
        std::int64_t delay = options_.retry_delay.count();
        for (unsigned i = 1; i < attempts_; ++i) delay = std::min<std::int64_t>(delay * 3, 86400000);
        retry_at_ = Clock::now() + Milliseconds{delay};
        state_ = SearchState::Retrying;
    } else state_ = successful_response_ || !had_error_ ? SearchState::NotFound : SearchState::Failed;
}

void Resolver::consider(Candidate candidate) {
    const auto evaluation = evaluate(query_, candidate);
    if (!evaluation.accepted) return;
    auto parsed = parse_lyrics(candidate.synced.empty() ? candidate.plain : candidate.synced,
        provider_name(candidate.provider), {options_.max_response_bytes, options_.max_lyric_lines});
    if (!parsed) return;
    if (candidate.instrumental) detail_lyrics_access::instrumental(*parsed);
    if (parsed->empty()) return;
    if (parsed->synced() || parsed->instrumental()) {
        if (evaluation.high_confidence) {
            set_document(std::move(*parsed));
            http_->cancel(); jobs_.clear(); followup_.reset();
            state_ = SearchState::Finished;
        } else if (evaluation.score > best_synced_score_) {
            best_synced_score_ = evaluation.score;
            best_synced_ = std::move(*parsed);
        }
    } else if (evaluation.score > best_plain_score_) {
        best_plain_score_ = evaluation.score;
        set_document(std::move(*parsed));
    }
}

void Resolver::receive(HttpResponse response) {
    const auto at = jobs_.find(response.id);
    if (at == jobs_.end()) return; // Canceled track or an earlier winning response.
    auto job = std::move(at->second);
    jobs_.erase(at);
    if (response.error != CURLE_OK) {
        had_error_ = true;
        error_ = Error{ErrorCode::Network, static_cast<int>(response.error), curl_easy_strerror(response.error)};
        return;
    }
    if (response.status < 200 || response.status >= 300) {
        if (response.status == 404) successful_response_ = true;
        else {
            had_error_ = true;
            error_ = Error{ErrorCode::Network, static_cast<int>(response.status), "Lyrics provider returned HTTP " + std::to_string(response.status)};
        }
        return;
    }
    auto candidates = parse_candidates(response.body, job.metadata.provider, job.stage);
    if (!candidates) {
        had_error_ = true;
        error_ = Error{ErrorCode::Network, 0, std::move(candidates.error())};
        return;
    }
    successful_response_ = true;
    if (search_stage(job.stage)) {
        struct Rated { int score; std::size_t index; };
        std::vector<Rated> rated;
        for (std::size_t i = 0; i < candidates->size(); ++i) {
            const auto evaluation = evaluate(query_, (*candidates)[i]);
            if (evaluation.accepted) rated.push_back({evaluation.score, i});
        }
        std::stable_sort(rated.begin(), rated.end(), [](auto a, auto b) { return a.score > b.score; });
        const auto stage = job.stage == Stage::NeteaseSearch ? Stage::NeteaseLyric
            : job.stage == Stage::QqSearch ? Stage::QqLyric : Stage::KugouLyricSearch;
        for (std::size_t i = 0; i < std::min(rated.size(), std::size_t{5}); ++i)
            schedule(stage, std::move((*candidates)[rated[i].index]));
    } else if (job.stage == Stage::KugouLyricSearch) {
        if (!candidates->empty()) {
            auto candidate = std::move(job.metadata);
            candidate.resource_id = candidates->front().resource_id;
            candidate.access_key = candidates->front().access_key;
            if (!candidate.resource_id.empty() && !candidate.access_key.empty()) schedule(Stage::KugouDownload, std::move(candidate));
        }
    } else {
        for (auto& candidate : *candidates) {
            if (candidate.title.empty()) candidate.title = job.metadata.title;
            if (candidate.artist.empty()) candidate.artist = job.metadata.artist;
            if (candidate.album.empty()) candidate.album = job.metadata.album;
            if (!candidate.duration) candidate.duration = job.metadata.duration;
            consider(std::move(candidate));
            if (state_ == SearchState::Finished) break;
        }
    }
}

std::expected<void, Error> Resolver::process(std::span<const pollfd> ready, Clock::time_point now) {
    if (!http_ || state_ != SearchState::Searching) return {};
    auto responses = http_->process(ready, now);
    if (!responses) {
        http_->cancel(); jobs_.clear(); followup_.reset();
        error_ = responses.error();
        had_error_ = true;
        complete();
        return {};
    }
    for (auto& response : *responses) receive(std::move(response));
    if (state_ == SearchState::Searching && jobs_.empty() && !http_->busy()) {
        if (followup_) {
            const auto stage = *followup_;
            followup_.reset();
            Candidate candidate;
            candidate.provider = options_.providers[provider_index_ - 1];
            if (candidate.provider == Provider::Musixmatch) {
                candidate.title = query_.title; candidate.artist = query_.artist;
                candidate.album = query_.album; candidate.duration = query_.duration;
            }
            schedule(stage, std::move(candidate));
        } else next_provider();
    }
    return {};
}

std::span<const pollfd> Resolver::poll_fds() const noexcept {
    return http_ ? http_->poll_fds() : std::span<const pollfd>{};
}

std::optional<Clock::time_point> Resolver::deadline() const noexcept {
    auto deadline = http_ ? http_->deadline() : std::nullopt;
    if (retry_at_ && (!deadline || *retry_at_ < *deadline)) deadline = retry_at_;
    return deadline;
}

std::optional<Error> Resolver::take_error() {
    auto error = std::move(error_);
    error_.reset();
    return error;
}

} // namespace lyricsmpris::detail
