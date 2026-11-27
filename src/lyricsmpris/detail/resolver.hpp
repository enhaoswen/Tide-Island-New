#pragma once

#include <cstdint>
#include <unordered_map>

#include "providers.hpp"

namespace LyricsMpris {

/// @brief Keeps instrumental handling out of the public document mutation API.
// Keeps instrumental handling out of the public document mutation API.
class DetailLyricsAccess {
public:
	/// @brief Marks a lyrics document as instrumental.
	static void set_instrumental(Lyrics& lyrics) { lyrics.is_instrumental_ = true; }
};

}

namespace LyricsMpris::Detail {

/// @brief Coordinates provider searches and lyrics selection.
class Resolver {
public:
	/// @brief Creates a resolver with the given options.
	static std::expected<std::unique_ptr<Resolver>, Error> create(const Options& options);
	/// @brief Starts a search for the given track.
	void start(TrackQuery query, std::string_view inline_lyrics, std::string_view media_url, bool is_fresh);
	/// @brief Clears the current search and document.
	void clear();

	/// @brief Resets the retry attempt counter.
	void reset_attempts() { attempts_ = 0; }

	/// @brief Drives HTTP responses and advances the search.
	std::expected<void, Error> process(std::span<const pollfd> ready, Clock::time_point now);

	/// @brief Returns true when a retry is due at now.
	bool is_retry_due(Clock::time_point now) const { return retry_at_ && now >= *retry_at_; }

	/// @brief Returns the descriptors the resolver is interested in.
	std::span<const pollfd> get_poll_fds() const;
	/// @brief Returns the next search deadline, if any.
	std::optional<Clock::time_point> get_deadline() const;

	/// @brief Returns the current lyrics document.
	const Lyrics& get_document() const { return document_; }

	/// @brief Returns the current search state.
	SearchState get_state() const { return state_; }

	/// @brief Returns the revision incremented when the document changes.
	uint64_t get_document_revision() const { return document_revision_; }

	/// @brief Takes the last search error, if any.
	std::optional<Error> take_error();

private:
	struct Job {
		Stage     stage_{};
		Candidate metadata_{};
	};

	explicit Resolver(const Options& options) :
		options_{options} {}

	bool ensure_http();
	void release_idle_http();
	void schedule(Stage stage, Candidate candidate = {});
	void next_provider();
	void complete();
	void consider(Candidate candidate);
	void set_document(Lyrics document);
	bool direct(std::string_view text, std::string_view provider);
	void local(std::string_view media_url);
	void receive(HttpResponse response);

	const Options&                    options_;
	std::unique_ptr<Http>             http_{};
	TrackQuery                        query_{};
	Lyrics                            document_, best_synced_;
	int                               best_synced_score_{-1};
	int                               best_plain_score_{-1};
	std::unordered_map<uint64_t, Job> jobs_{};
	uint64_t                          next_id_{};
	uint64_t                          document_revision_{};
	size_t                            provider_index_{};
	std::optional<Stage>              followup_{};
	std::optional<Clock::time_point>  retry_at_{};
	unsigned                          attempts_{};
	SearchState                       state_{SearchState::Idle};
	bool                              has_successful_response_{};
	bool                              has_error_occurred_{};
	std::optional<Error>              error_{};
};

}
