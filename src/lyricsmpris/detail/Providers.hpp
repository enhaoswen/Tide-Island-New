#pragma once

#include "Http.hpp"

namespace lyricsmpris::detail {

enum class Stage {
    LrclibGet, LrclibSearch, LrcxJson, LrcxText, NeteaseSearch, NeteaseLyric,
    QqSearch, QqLyric, KugouSearch, KugouLyricSearch, KugouDownload,
    MusixmatchSubtitle, MusixmatchPlain
};

struct TrackQuery {
    std::string title, artist, album;
    std::optional<Milliseconds> duration;
};

struct Candidate {
    Provider provider = Provider::Lrclib;
    std::string title, artist, album;
    std::optional<Milliseconds> duration;
    std::string synced, plain;
    std::string resource_id, access_key;
    bool instrumental = false;
    bool trusted = true;
};

struct Evaluation {
    int score = 0;
    bool accepted = false;
    bool high_confidence = false;
};

std::string_view provider_name(Provider provider) noexcept;
Evaluation evaluate(const TrackQuery& query, const Candidate& candidate);
std::expected<std::vector<Candidate>, std::string> parse_candidates(
    std::string_view payload, Provider provider, Stage stage);
HttpRequest provider_request(std::uint64_t id, Stage stage, const TrackQuery& query,
    const Candidate& candidate, std::string_view api_key = {});
std::string decode_base64(std::string_view encoded);

} // namespace lyricsmpris::detail
