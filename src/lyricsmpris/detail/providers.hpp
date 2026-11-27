#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "http.hpp"

namespace LyricsMpris::Detail {

/// @brief Identifies a provider request stage.
enum class Stage : uint8_t {
	LrclibGet,
	LrclibSearch,
	LrcxJson,
	LrcxText,
	NeteaseSearch,
	NeteaseLyric,
	QqSearch,
	QqLyric,
	KugouSearch,
	KugouLyricSearch,
	KugouDownload,
	MusixmatchSubtitle,
	MusixmatchPlain
};

/// @brief Track metadata used to query and evaluate providers.
struct TrackQuery {
	std::string                 title_{}, artist_{}, album_{};
	std::optional<Milliseconds> duration_{};
};

/// @brief A lyrics candidate returned by a provider.
struct Candidate {
	Provider                    provider_{Provider::Lrclib};
	std::string                 title_{}, artist_{}, album_{};
	std::optional<Milliseconds> duration_{};
	std::string                 synced_{}, plain_{};
	std::string                 resource_id_{}, access_key_{};
	bool                        is_instrumental_{};
	bool                        is_trusted_{true};
};

/// @brief Score and acceptance decision for a candidate.
struct Evaluation {
	int  score_{};
	bool is_accepted_{};
	bool is_high_confidence_{};
};

/// @brief Returns the stable name of a provider.
std::string_view provider_name(Provider provider);
/// @brief Scores a candidate against the track query.
Evaluation evaluate(const TrackQuery& query, const Candidate& candidate);
/// @brief Parses provider payloads into candidates.
std::expected<std::vector<Candidate>, std::string>
parse_candidates(std::string_view payload, Provider provider, Stage stage);
/// @brief Builds the HTTP request for a provider stage.
HttpRequest provider_request(uint64_t id,
	Stage                             stage,
	const TrackQuery&                 query,
	const Candidate&                  candidate,
	std::string_view                  api_key = {});
/// @brief Decodes a base64 payload, returning an empty string on invalid input.
std::string decode_base64(std::string_view encoded);

}
