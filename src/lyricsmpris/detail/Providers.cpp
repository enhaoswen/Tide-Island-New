#include "Providers.hpp"
#include "Normalize.hpp"
#include "Utility.hpp"
#include "ProviderResponses.hpp"

#include <algorithm>
#include <cmath>

namespace lyricsmpris::detail {
namespace {

struct JsonOptions : glz::opts {
    bool validate_trailing_whitespace = true;
    bool validate_skipped = true;
};

constexpr auto json_options = [] {
    JsonOptions options;
    options.null_terminated = false;
    options.error_on_unknown_keys = false;
    return options;
}();

template<class T, class... Alternatives>
const T& value(const std::variant<Alternatives...>& field) {
    if (const auto* result = std::get_if<T>(&field)) return *result;
    static const T empty{};
    return empty;
}

std::string string(const responses::Scalar& field) {
    if (const auto* text = std::get_if<std::string>(&field)) return *text;
    if (const auto* integer = std::get_if<std::uint64_t>(&field)) return std::to_string(*integer);
    if (const auto* integer = std::get_if<std::int64_t>(&field)) return std::to_string(*integer);
    return {};
}

double numeric(const responses::Scalar& field) {
    if (const auto* text = std::get_if<std::string>(&field)) return number<double>(*text).value_or(0);
    if (const auto* integer = std::get_if<std::uint64_t>(&field)) return static_cast<double>(*integer);
    if (const auto* integer = std::get_if<std::int64_t>(&field)) return static_cast<double>(*integer);
    if (const auto* real = std::get_if<double>(&field)) return *real;
    return 0;
}

bool boolean(const responses::Scalar& field) {
    if (const auto* flag = std::get_if<bool>(&field)) return *flag;
    if (const auto* text = std::get_if<std::string>(&field)) return !text->empty();
    return numeric(field) != 0;
}

std::optional<Milliseconds> duration(double value, double multiplier = 1) {
    value *= multiplier;
    if (!std::isfinite(value) || value <= 0 || value > 86400000.0) return {};
    return Milliseconds{static_cast<std::int64_t>(std::llround(value))};
}

std::string first(std::initializer_list<const responses::Scalar*> fields) {
    for (const auto* field : fields) {
        auto text = string(*field);
        if (!trim(text).empty()) return text;
    }
    return {};
}

std::string artists(const responses::Array<responses::Artist>& field) {
    std::string result;
    for (const auto& element : value<std::vector<responses::Object<responses::Artist>>>(field)) {
        auto name = std::holds_alternative<std::string>(element)
            ? std::get<std::string>(element) : string(value<responses::Artist>(element).name);
        if (name.empty()) continue;
        if (!result.empty()) result += ", ";
        result += name;
    }
    return result;
}

template<class Response, class Extract>
std::expected<std::vector<Candidate>, std::string> response(
    std::string_view payload, Provider provider, Extract extract) {
    Response document{};
    if (const auto error = glz::read<json_options>(document, payload))
        return std::unexpected(glz::format_error(error, payload));
    std::vector<Candidate> result;
    auto append = [&](Candidate candidate) {
        if (candidate.synced.empty() && candidate.plain.empty()
            && candidate.resource_id.empty() && !candidate.instrumental) return;
        candidate.provider = provider;
        result.push_back(std::move(candidate));
    };
    extract(document, append);
    return result;
}

template<class Row, class Convert, class Append>
void rows(const std::vector<responses::Object<Row>>& documents, Convert convert, Append append) {
    // Retain at most 64 search results regardless of payload size.
    for (std::size_t i = 0; i < std::min(documents.size(), std::size_t{64}); ++i)
        if (const auto* row = std::get_if<Row>(&documents[i])) append(convert(*row));
}

template<class Row, class Convert>
std::expected<std::vector<Candidate>, std::string> direct_response(
    std::string_view payload, Provider provider, Convert convert) {
    return response<responses::Rows<Row>>(payload, provider, [&](const auto& document, auto append) {
        if (const auto* row = std::get_if<Row>(&document)) append(convert(*row));
        else rows(value<std::vector<responses::Object<Row>>>(document), convert, append);
    });
}

template<class Row, class Convert, class Append>
void rows(const responses::Array<Row>& documents, Convert convert, Append append) {
    if (const auto* row = std::get_if<Row>(&documents)) append(convert(*row));
    else rows(value<std::vector<responses::Object<Row>>>(documents), convert, append);
}

std::string encode(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size());
    for (const unsigned char byte : value) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
            || (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' || byte == '~')
            result += static_cast<char>(byte);
        else { result += '%'; result += hex[byte >> 4]; result += hex[byte & 15]; }
    }
    return result;
}

void parameter(std::string& url, std::string_view key, std::string_view value) {
    url += url.find('?') == url.npos ? '?' : '&';
    url += key;
    url += '=';
    url += encode(value);
}

std::optional<std::int64_t> ttml_time(std::string_view value) {
    double multiplier = 1000;
    if (value.ends_with("ms")) { value.remove_suffix(2); multiplier = 1; }
    else if (value.ends_with('s')) value.remove_suffix(1);
    double total = 0;
    unsigned parts = 0;
    while (true) {
        if (++parts > 3) return {};
        const auto colon = value.find(':');
        const auto part = number<double>(value.substr(0, colon));
        if (!part || !std::isfinite(*part) || *part < 0) return {};
        total = total * 60 + *part;
        if (colon == value.npos) break;
        value.remove_prefix(colon + 1);
    }
    total *= multiplier;
    if (!std::isfinite(total) || total > 86400000) return {};
    return static_cast<std::int64_t>(std::llround(total));
}

std::string ttml_to_lrc(std::string_view xml) {
    std::string result;
    while (!xml.empty()) {
        const auto start = xml.find("<p");
        if (start == xml.npos) break;
        xml.remove_prefix(start + 2);
        if (!xml.empty() && xml.front() != ' ' && xml.front() != '\t' && xml.front() != '\n' && xml.front() != '>') continue;
        const auto close = xml.find('>');
        if (close == xml.npos) break;
        const auto header = xml.substr(0, close);
        const auto begin = header.find("begin=");
        const auto end = xml.find("</p>", close);
        if (end == xml.npos) break;
        if (begin != header.npos && begin + 6 < header.size()) {
            const auto quote = header[begin + 6];
            const auto finish = header.find(quote, begin + 7);
            if ((quote == '\'' || quote == '"') && finish != header.npos) {
                if (auto time = ttml_time(header.substr(begin + 7, finish - begin - 7))) {
                    const auto milliseconds = *time % 1000;
                    result += '[' + std::to_string(*time / 60000) + ':';
                    if ((*time / 1000) % 60 < 10) result += '0';
                    result += std::to_string((*time / 1000) % 60) + '.';
                    if (milliseconds < 100) result += '0';
                    if (milliseconds < 10) result += '0';
                    result += std::to_string(milliseconds) + ']';
                    result += clean_text(xml.substr(close + 1, end - close - 1));
                    result += '\n';
                }
            }
        }
        xml.remove_prefix(end + 4);
    }
    return result;
}

TrackQuery metadata(std::string_view text) {
    TrackQuery result;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto row = trim(text.substr(0, end));
        if (end == text.npos) text = {};
        else text.remove_prefix(end + 1);
        if (!row.starts_with('[') || !row.ends_with(']')) continue;
        row.remove_prefix(1); row.remove_suffix(1);
        if (row.starts_with("ti:")) result.title = clean_text(row.substr(3));
        if (row.starts_with("ar:")) result.artist = clean_text(row.substr(3));
        if (row.starts_with("al:")) result.album = clean_text(row.substr(3));
    }
    return result;
}

} // namespace

std::string_view provider_name(Provider provider) noexcept {
    switch (provider) {
    case Provider::Lrclib: return "lrclib";
    case Provider::Lrcx: return "lrcx";
    case Provider::Netease: return "netease";
    case Provider::Qq: return "qq";
    case Provider::Kugou: return "kugou";
    case Provider::Musixmatch: return "musixmatch";
    }
    return {};
}

Evaluation evaluate(const TrackQuery& query, const Candidate& candidate) {
    Evaluation result;
    if (normalize(query.title, true).empty()) return result;
    const auto query_flags = version_flags(query.title + ' ' + query.album);
    // A provider's instrumental flag means this recording has no vocals;
    // its title need not explicitly contain "instrumental". Explicit alternate
    // versions in the title/album still participate in mismatch rejection.
    const auto candidate_flags = version_flags(candidate.title + ' ' + candidate.album);
    if ((candidate_flags & ~query_flags) != 0) return result;
    auto evidence = metadata(candidate.synced);
    if (evidence.title.empty() && evidence.artist.empty()) evidence = metadata(candidate.plain);
    double title = 0, artist = 0, album = 0;
    bool has_title = false;
    for (const auto value : {candidate.trusted ? std::string_view(candidate.title) : std::string_view{}, std::string_view(evidence.title)}) {
        if (trim(value).empty()) continue;
        const auto score = similarity(query.title, value, true);
        if (score < 0.72) return result;
        title = std::max(title, score);
        has_title = true;
    }
    if (!has_title) return result;
    if (!trim(query.artist).empty()) {
        for (const auto value : {candidate.trusted ? std::string_view(candidate.artist) : std::string_view{}, std::string_view(evidence.artist)}) {
            if (trim(value).empty()) continue;
            auto score = similarity(query.artist, value, false);
            const bool listed = contains_tokens(query.artist, value);
            if (score < 0.60 && !listed && comparable_script(query.artist, value)) return result;
            if (listed) score = std::max(score, 0.92);
            artist = std::max(artist, score);
        }
    }
    if (!query.album.empty()) {
        if (candidate.trusted) album = similarity(query.album, candidate.album, true);
        album = std::max(album, similarity(query.album, evidence.album, true));
    }
    result.score = static_cast<int>(std::lround(title * 60 + artist * 25));
    if (album >= 0.6) result.score += static_cast<int>(std::lround(album * 8));
    if (query.duration) {
        if (candidate.duration) {
            const auto delta = std::abs((*query.duration - *candidate.duration).count());
            const bool versioned = (query_flags | candidate_flags) != 0;
            const auto tolerance = std::max<std::int64_t>(versioned ? 30000 : 15000,
                static_cast<std::int64_t>(query.duration->count() * (versioned ? 0.15 : 0.08)));
            if (delta > tolerance) return {};
            result.score += delta <= 2000 ? 20 : delta <= 5000 ? 15 : delta <= 10000 ? 10 : 5;
        } else if (*query.duration < Milliseconds{90000}) return {};
    }
    if (!candidate.synced.empty() || !candidate.resource_id.empty()) result.score += 8;
    else if (!candidate.plain.empty()) result.score += 2;
    if (candidate.instrumental) result.score += 4;
    result.accepted = result.score >= ((!candidate.synced.empty() || candidate.instrumental || !candidate.resource_id.empty()) ? 72 : 68);
    result.high_confidence = result.accepted && result.score >= 92 && title >= 0.92
        && (trim(query.artist).empty() || artist >= 0.9) && (!query.duration || candidate.duration);
    return result;
}

std::string decode_base64(std::string_view encoded) {
    std::string output;
    output.reserve(encoded.size() / 4 * 3);
    unsigned bits = 0, count = 0;
    bool padding = false;
    for (unsigned char c : encoded) {
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        if (c == '=') { padding = true; continue; }
        if (padding) return {};
        unsigned value;
        if (c >= 'A' && c <= 'Z') value = c - 'A';
        else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
        else if (c >= '0' && c <= '9') value = c - '0' + 52;
        else if (c == '+') value = 62;
        else if (c == '/') value = 63;
        else return {};
        bits = (bits << 6) | value;
        count += 6;
        if (count >= 8) { count -= 8; output += static_cast<char>((bits >> count) & 255); }
    }
    if (count == 6) return {};
    return output;
}

std::expected<std::vector<Candidate>, std::string> parse_candidates(std::string_view payload, Provider provider, Stage stage) {
    std::vector<Candidate> result;
    if (stage == Stage::LrcxText) {
        Candidate candidate;
        candidate.provider = provider;
        candidate.synced = payload;
        candidate.trusted = false;
        result.push_back(std::move(candidate));
        return result;
    }
    if (trim(payload).empty()) return std::unexpected("Empty JSON response");
    using namespace responses;
    switch (stage) {
    case Stage::LrclibGet:
    case Stage::LrclibSearch:
        return direct_response<Lrclib>(payload, provider, [](const auto& row) {
            Candidate candidate;
            candidate.title = first({&row.trackName, &row.name});
            candidate.artist = first({&row.artistName, &row.artist});
            candidate.album = first({&row.albumName, &row.album});
            candidate.duration = duration(numeric(row.duration), 1000);
            candidate.synced = string(row.syncedLyrics);
            candidate.plain = string(row.plainLyrics);
            candidate.instrumental = boolean(row.instrumental);
            return candidate;
        });
    case Stage::LrcxJson:
        return direct_response<Lrcx>(payload, provider, [](const auto& row) {
            Candidate candidate;
            candidate.title = first({&row.title, &row.trackName, &row.name});
            candidate.artist = first({&row.artist, &row.artistName});
            candidate.album = first({&row.album, &row.albumName});
            const auto seconds = numeric(row.duration);
            candidate.duration = duration(seconds, seconds < 10000 ? 1000 : 1);
            candidate.synced = first({&row.lyrics, &row.lyric, &row.lrc, &row.syncedLyrics});
            if (candidate.synced.empty()) candidate.synced = ttml_to_lrc(string(row.lrc_ttml));
            candidate.plain = string(row.plainLyrics);
            return candidate;
        });
    case Stage::NeteaseSearch:
        return response<Object<NeteaseSearch>>(payload, provider, [](const auto& document, auto append) {
            const auto& songs = value<NeteaseResult>(value<NeteaseSearch>(document).result).songs;
            rows(songs, [](const auto& row) {
                Candidate candidate;
                candidate.title = string(row.name);
                candidate.artist = artists(row.artists);
                if (candidate.artist.empty()) candidate.artist = artists(row.ar);
                candidate.album = string(value<Album>(row.album).name);
                if (candidate.album.empty()) candidate.album = string(value<Album>(row.al).name);
                candidate.duration = duration(numeric(row.duration));
                if (!candidate.duration) candidate.duration = duration(numeric(row.dt));
                candidate.resource_id = string(row.id);
                return candidate;
            }, append);
        });
    case Stage::NeteaseLyric:
        return direct_response<NeteaseLyric>(payload, provider, [](const auto& row) {
            Candidate candidate;
            candidate.synced = string(value<Lyric>(row.lrc).lyric);
            candidate.plain = string(value<Lyric>(row.tlyric).lyric);
            return candidate;
        });
    case Stage::QqSearch:
        return response<Object<QqSearch>>(payload, provider, [](const auto& document, auto append) {
            const auto& songs = value<QqSongs>(value<QqData>(value<QqSearch>(document).data).song).list;
            rows(songs, [](const auto& row) {
                Candidate candidate;
                candidate.title = first({&row.songname, &row.title});
                candidate.artist = artists(row.singer);
                candidate.album = string(row.albumname);
                candidate.duration = duration(numeric(row.interval), 1000);
                candidate.resource_id = first({&row.songmid, &row.mid});
                return candidate;
            }, append);
        });
    case Stage::QqLyric:
        return direct_response<QqLyric>(payload, provider, [](const auto& row) {
            Candidate candidate;
            candidate.synced = string(row.lyric);
            if (candidate.synced.find('[') == candidate.synced.npos) {
                if (auto decoded = decode_base64(candidate.synced); !decoded.empty() && valid_utf8(decoded))
                    candidate.synced = std::move(decoded);
            }
            return candidate;
        });
    case Stage::KugouSearch:
        return response<Object<KugouSearch>>(payload, provider, [](const auto& document, auto append) {
            const auto& songs = value<KugouData>(value<KugouSearch>(document).data).lists;
            rows(songs, [](const auto& row) {
                Candidate candidate;
                candidate.title = clean_text(first({&row.SongName, &row.FileName}));
                candidate.artist = clean_text(string(row.SingerName));
                candidate.album = clean_text(string(row.AlbumName));
                candidate.duration = duration(numeric(row.Duration), 1000);
                candidate.resource_id = first({&row.FileHash, &row.Hash});
                return candidate;
            }, append);
        });
    case Stage::KugouLyricSearch:
        return response<Object<KugouLyricSearch>>(payload, provider, [](const auto& document, auto append) {
            rows(value<KugouLyricSearch>(document).candidates,
                [](const auto& row) {
                    Candidate candidate;
                    candidate.resource_id = string(row.id);
                    candidate.access_key = string(row.accesskey);
                    return candidate;
                }, append);
        });
    case Stage::KugouDownload:
        return direct_response<KugouDownload>(payload, provider, [](const auto& row) {
            Candidate candidate;
            candidate.synced = string(row.content);
            if (auto decoded = decode_base64(candidate.synced); !decoded.empty()) candidate.synced = std::move(decoded);
            return candidate;
        });
    case Stage::MusixmatchSubtitle:
    case Stage::MusixmatchPlain:
        return direct_response<Musixmatch>(payload, provider, [](const auto& row) {
            const auto& body = value<MusixmatchBody>(value<MusixmatchMessage>(row.message).body);
            Candidate candidate;
            candidate.synced = string(value<Subtitle>(body.subtitle).subtitle_body);
            candidate.plain = string(value<PlainLyrics>(body.lyrics).lyrics_body);
            return candidate;
        });
    case Stage::LrcxText: break;
    }
    return result;
}

HttpRequest provider_request(std::uint64_t id, Stage stage, const TrackQuery& query, const Candidate& candidate, std::string_view api_key) {
    HttpRequest request{id, {}, {}};
    auto& url = request.url;
    auto add = [&](auto key, std::string_view value) { parameter(url, key, value); };
    const auto seconds = query.duration ? std::to_string(std::max<std::int64_t>(1, query.duration->count() / 1000)) : "";
    switch (stage) {
    case Stage::LrclibGet:
    case Stage::LrclibSearch:
        url = stage == Stage::LrclibGet ? "https://lrclib.net/api/get" : "https://lrclib.net/api/search";
        add("track_name", query.title); add("artist_name", query.artist);
        if (stage == Stage::LrclibGet) {
            if (!query.album.empty()) add("album_name", query.album);
            if (!seconds.empty()) add("duration", seconds);
        }
        break;
    case Stage::LrcxJson:
    case Stage::LrcxText:
        url = stage == Stage::LrcxJson ? "https://api.lrc.cx/jsonapi" : "https://api.lrc.cx/lyrics";
        add("title", query.title); add("artist", query.artist);
        if (stage == Stage::LrcxJson) {
            if (!query.album.empty()) add("album", query.album);
            if (!seconds.empty()) add("duration", seconds);
        }
        break;
    case Stage::NeteaseSearch:
        url = "https://music.163.com/api/search/get";
        add("s", query.title + ' ' + query.artist); add("type", "1"); add("limit", "5"); add("offset", "0");
        break;
    case Stage::NeteaseLyric:
        url = "https://music.163.com/api/song/lyric";
        add("os", "pc"); add("id", candidate.resource_id); add("lv", "-1"); add("tv", "-1");
        break;
    case Stage::QqSearch:
        url = "https://c.y.qq.com/soso/fcgi-bin/client_search_cp";
        add("format", "json"); add("p", "1"); add("n", "5"); add("w", query.title + ' ' + query.artist);
        break;
    case Stage::QqLyric:
        url = "https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg";
        add("songmid", candidate.resource_id); add("format", "json"); add("nobase64", "1");
        break;
    case Stage::KugouSearch:
        url = "https://songsearch.kugou.com/song_search_v2";
        add("keyword", query.title + ' ' + query.artist); add("page", "1"); add("pagesize", "5");
        break;
    case Stage::KugouLyricSearch:
        url = "https://lyrics.kugou.com/search";
        add("ver", "1"); add("man", "yes"); add("client", "pc"); add("keyword", candidate.title + ' ' + candidate.artist);
        add("duration", std::to_string(candidate.duration ? candidate.duration->count() : 1)); add("hash", candidate.resource_id);
        break;
    case Stage::KugouDownload:
        url = "https://lyrics.kugou.com/download";
        add("ver", "1"); add("client", "pc"); add("id", candidate.resource_id); add("accesskey", candidate.access_key);
        add("fmt", "lrc"); add("charset", "utf8");
        break;
    case Stage::MusixmatchSubtitle:
    case Stage::MusixmatchPlain:
        url = stage == Stage::MusixmatchSubtitle ? "https://api.musixmatch.com/ws/1.1/matcher.subtitle.get"
            : "https://api.musixmatch.com/ws/1.1/matcher.lyrics.get";
        add("q_track", query.title); add("q_artist", query.artist); add("apikey", api_key);
        break;
    }
    if (stage == Stage::NeteaseSearch || stage == Stage::NeteaseLyric) request.referer = "https://music.163.com/";
    if (stage == Stage::QqSearch || stage == Stage::QqLyric) request.referer = "https://y.qq.com/";
    if (stage == Stage::KugouSearch || stage == Stage::KugouLyricSearch || stage == Stage::KugouDownload) request.referer = "https://www.kugou.com/";
    return request;
}

} // namespace lyricsmpris::detail
