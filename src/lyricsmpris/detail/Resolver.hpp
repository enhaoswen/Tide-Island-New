#pragma once

#include "Providers.hpp"

#include <unordered_map>

namespace lyricsmpris {
// Keeps instrumental handling out of the public document mutation API.
class detail_lyrics_access {
public:
    static void instrumental(Lyrics& lyrics) { lyrics.instrumental_ = true; }
};
}

namespace lyricsmpris::detail {

class Resolver {
public:
    static std::expected<std::unique_ptr<Resolver>, Error> create(const Options& options);
    void start(TrackQuery query, std::string_view inline_lyrics, std::string_view media_url, bool fresh);
    void clear();
    void reset_attempts() noexcept { attempts_ = 0; }
    std::expected<void, Error> process(std::span<const pollfd> ready, Clock::time_point now);
    bool retry_due(Clock::time_point now) const noexcept { return retry_at_ && now >= *retry_at_; }
    std::span<const pollfd> poll_fds() const noexcept;
    std::optional<Clock::time_point> deadline() const noexcept;
    const Lyrics& document() const noexcept { return document_; }
    SearchState state() const noexcept { return state_; }
    std::uint64_t document_revision() const noexcept { return document_revision_; }
    std::optional<Error> take_error();

private:
    struct Job { Stage stage; Candidate metadata; };
    explicit Resolver(const Options& options) : options_(options) {}
    void schedule(Stage stage, Candidate candidate = {});
    void next_provider();
    void complete();
    void consider(Candidate candidate);
    void set_document(Lyrics document);
    bool direct(std::string_view text, std::string_view provider);
    void local(std::string_view media_url);
    void receive(HttpResponse response);

    const Options& options_;
    std::unique_ptr<Http> http_;
    TrackQuery query_;
    Lyrics document_, best_synced_;
    int best_synced_score_ = -1;
    int best_plain_score_ = -1;
    std::unordered_map<std::uint64_t, Job> jobs_;
    std::uint64_t next_id_ = 0;
    std::uint64_t document_revision_ = 0;
    std::size_t provider_index_ = 0;
    std::optional<Stage> followup_;
    std::optional<Clock::time_point> retry_at_;
    unsigned attempts_ = 0;
    SearchState state_ = SearchState::Idle;
    bool successful_response_ = false;
    bool had_error_ = false;
    std::optional<Error> error_;
};

} // namespace lyricsmpris::detail
