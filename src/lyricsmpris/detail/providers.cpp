#include "providers.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "normalize.hpp"
#include "provider_responses.hpp"
#include "utility.hpp"

using namespace std;

namespace LyricsMpris::Detail {
namespace {

struct JsonOptions : glz::opts {
	bool validate_trailing_whitespace{true};
	bool validate_skipped{true};
};

constexpr auto k_json_options = [] {
	JsonOptions options{};
	options.null_terminated       = false;
	options.error_on_unknown_keys = false;
	return options;
}();

template<class T, class... Alternatives>
const T& value(const variant<Alternatives...>& field) {
	if(const auto* result = get_if<T>(&field))
		return *result;
	static const T empty{};
	return empty;
}

string scalar_string(const Responses::Scalar& field) {
	if(const auto* text = get_if<string>(&field))
		return *text;
	if(const auto* integer = get_if<uint64_t>(&field))
		return to_string(*integer);
	if(const auto* integer = get_if<int64_t>(&field))
		return to_string(*integer);
	return {};
}

double numeric(const Responses::Scalar& field) {
	if(const auto* text = get_if<string>(&field))
		return number<double>(*text).value_or(0);
	if(const auto* integer = get_if<uint64_t>(&field))
		return static_cast<double>(*integer);
	if(const auto* integer = get_if<int64_t>(&field))
		return static_cast<double>(*integer);
	if(const auto* real = get_if<double>(&field))
		return *real;
	return 0;
}

bool is_truthy(const Responses::Scalar& field) {
	if(const auto* flag = get_if<bool>(&field))
		return *flag;
	if(const auto* text = get_if<string>(&field))
		return !text->empty();
	return numeric(field) != 0;
}

optional<Milliseconds> duration(double value, double multiplier = 1) {
	value *= multiplier;
	if(!isfinite(value) || value <= 0 || value > 86400000.0)
		return {};
	return Milliseconds{static_cast<int64_t>(llround(value))};
}

string first(initializer_list<const Responses::Scalar*> fields) {
	for(const auto* field : fields) {
		const auto text{scalar_string(*field)};
		if(!trim(text).empty())
			return text;
	}
	return {};
}

string artists(const Responses::Array<Responses::Artist>& field) {
	string result{};
	for(const auto& element : value<vector<Responses::Object<Responses::Artist>>>(field)) {
		const auto name = holds_alternative<string>(element) ? get<string>(element)
															 : scalar_string(value<Responses::Artist>(element).name);
		if(name.empty())
			continue;
		if(!result.empty())
			result += ", ";
		result += name;
	}
	return result;
}

template<class Response, class Extract>
expected<vector<Candidate>, string> response(string_view payload, Provider provider, Extract extract) {
	Response document{};
	if(const auto error = glz::read<k_json_options>(document, payload))
		return unexpected(glz::format_error(error, payload));
	vector<Candidate> result{};
	const auto        append = [&](Candidate candidate) {
		if(candidate.synced_.empty() && candidate.plain_.empty() && candidate.resource_id_.empty() &&
			!candidate.is_instrumental_)
			return;
		candidate.provider_ = provider;
		result.push_back(std::move(candidate));
	};
	extract(document, append);
	return result;
}

template<class Row, class Convert, class Append>
void rows(const vector<Responses::Object<Row>>& documents, Convert convert, Append append) {
	// Retain at most 64 search results regardless of payload size.
	for(size_t i{}; i < min(documents.size(), size_t{64}); ++i)
		if(const auto* row = get_if<Row>(&documents[i]))
			append(convert(*row));
}

template<class Row, class Convert>
expected<vector<Candidate>, string> direct_response(string_view payload, Provider provider, Convert convert) {
	return response<Responses::Rows<Row>>(payload, provider, [&](const auto& document, auto append) {
		if(const auto* row = get_if<Row>(&document))
			append(convert(*row));
		else
			rows(value<vector<Responses::Object<Row>>>(document), convert, append);
	});
}

template<class Row, class Convert, class Append>
void rows(const Responses::Array<Row>& documents, Convert convert, Append append) {
	if(const auto* row = get_if<Row>(&documents))
		append(convert(*row));
	else
		rows(value<vector<Responses::Object<Row>>>(documents), convert, append);
}

string encode(string_view value) {
	constexpr char k_hex[] = "0123456789ABCDEF";
	string         result{};
	result.reserve(value.size());
	for(const char raw : value) {
		const auto byte{static_cast<unsigned char>(raw)};
		if((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
			byte == '-' || byte == '_' || byte == '.' || byte == '~')
			result += static_cast<char>(byte);
		else {
			result += '%';
			result += k_hex[byte >> 4];
			result += k_hex[byte & 15];
		}
	}
	return result;
}

void parameter(string& url, string_view key, string_view value) {
	url += url.find('?') == url.npos ? '?' : '&';
	url += key;
	url += '=';
	url += encode(value);
}

optional<int64_t> ttml_time(string_view value) {
	double multiplier{1000};
	if(value.ends_with("ms")) {
		value.remove_suffix(2);
		multiplier = 1;
	} else if(value.ends_with('s'))
		value.remove_suffix(1);
	double   total{};
	unsigned parts{};
	while(true) {
		if(++parts > 3)
			return {};
		const auto colon{value.find(':')};
		const auto part{number<double>(value.substr(0, colon))};
		if(!part || !isfinite(*part) || *part < 0)
			return {};
		total = total * 60 + *part;
		if(colon == value.npos)
			break;
		value.remove_prefix(colon + 1);
	}
	total *= multiplier;
	if(!isfinite(total) || total > 86400000)
		return {};
	return static_cast<int64_t>(llround(total));
}

string ttml_to_lrc(string_view xml) {
	string result{};
	while(!xml.empty()) {
		const auto start{xml.find("<p")};
		if(start == xml.npos)
			break;
		xml.remove_prefix(start + 2);
		if(!xml.empty() && xml.front() != ' ' && xml.front() != '\t' && xml.front() != '\n' && xml.front() != '>')
			continue;
		const auto close{xml.find('>')};
		if(close == xml.npos)
			break;
		const auto header{xml.substr(0, close)};
		const auto begin{header.find("begin=")};
		const auto end{xml.find("</p>", close)};
		if(end == xml.npos)
			break;
		if(begin != header.npos && begin + 6 < header.size()) {
			const auto quote{header[begin + 6]};
			const auto finish{header.find(quote, begin + 7)};
			if((quote == '\'' || quote == '"') && finish != header.npos) {
				if(const auto time = ttml_time(header.substr(begin + 7, finish - begin - 7))) {
					const auto milliseconds{*time % 1000};
					result += '[' + to_string(*time / 60000) + ':';
					if((*time / 1000) % 60 < 10)
						result += '0';
					result += to_string((*time / 1000) % 60) + '.';
					if(milliseconds < 100)
						result += '0';
					if(milliseconds < 10)
						result += '0';
					result += to_string(milliseconds) + ']';
					result += clean_text(xml.substr(close + 1, end - close - 1));
					result += '\n';
				}
			}
		}
		xml.remove_prefix(end + 4);
	}
	return result;
}

TrackQuery metadata(string_view text) {
	TrackQuery result{};
	while(!text.empty()) {
		const auto end{text.find('\n')};
		auto       row{trim(text.substr(0, end))};
		if(end == text.npos)
			text = {};
		else
			text.remove_prefix(end + 1);
		if(!row.starts_with('[') || !row.ends_with(']'))
			continue;
		row.remove_prefix(1);
		row.remove_suffix(1);
		if(row.starts_with("ti:"))
			result.title_ = clean_text(row.substr(3));
		if(row.starts_with("ar:"))
			result.artist_ = clean_text(row.substr(3));
		if(row.starts_with("al:"))
			result.album_ = clean_text(row.substr(3));
	}
	return result;
}

}

string_view provider_name(Provider provider) {
	switch(provider) {
		case Provider::Lrclib:
			return "lrclib";
		case Provider::Lrcx:
			return "lrcx";
		case Provider::Netease:
			return "netease";
		case Provider::Qq:
			return "qq";
		case Provider::Kugou:
			return "kugou";
		case Provider::Musixmatch:
			return "musixmatch";
	}
	return {};
}

Evaluation evaluate(const TrackQuery& query, const Candidate& candidate) {
	Evaluation result{};
	if(normalize(query.title_, true).empty())
		return result;
	const auto query_flags{version_flags(query.title_ + ' ' + query.album_)};
	// A provider's instrumental flag means this recording has no vocals;
	// its title need not explicitly contain "instrumental". Explicit alternate
	// versions in the title/album still participate in mismatch rejection.
	const auto candidate_flags{version_flags(candidate.title_ + ' ' + candidate.album_)};
	if((candidate_flags & ~query_flags) != 0)
		return result;
	auto evidence{metadata(candidate.synced_)};
	if(evidence.title_.empty() && evidence.artist_.empty())
		evidence = metadata(candidate.plain_);
	double title{}, artist{}, album{};
	bool   has_title{};
	for(const auto value :
		{candidate.is_trusted_ ? string_view(candidate.title_) : string_view{}, string_view(evidence.title_)}) {
		if(trim(value).empty())
			continue;
		const auto score{similarity(query.title_, value, true)};
		if(score < 0.72)
			return result;
		title     = max(title, score);
		has_title = true;
	}
	if(!has_title)
		return result;
	if(!trim(query.artist_).empty()) {
		for(const auto value :
			{candidate.is_trusted_ ? string_view(candidate.artist_) : string_view{}, string_view(evidence.artist_)}) {
			if(trim(value).empty())
				continue;
			auto       score{similarity(query.artist_, value, false)};
			const bool is_listed{has_tokens(query.artist_, value)};
			if(score < 0.60 && !is_listed && has_comparable_script(query.artist_, value))
				return result;
			if(is_listed)
				score = max(score, 0.92);
			artist = max(artist, score);
		}
	}
	if(!query.album_.empty()) {
		if(candidate.is_trusted_)
			album = similarity(query.album_, candidate.album_, true);
		album = max(album, similarity(query.album_, evidence.album_, true));
	}
	result.score_ = static_cast<int>(lround(title * 60 + artist * 25));
	if(album >= 0.6)
		result.score_ += static_cast<int>(lround(album * 8));
	if(query.duration_) {
		if(candidate.duration_) {
			const auto delta{abs((*query.duration_ - *candidate.duration_).count())};
			const bool is_versioned{(query_flags | candidate_flags) != 0};
			const auto tolerance = max<int64_t>(is_versioned ? 30000 : 15000,
				static_cast<int64_t>(static_cast<double>(query.duration_->count()) * (is_versioned ? 0.15 : 0.08)));
			if(delta > tolerance)
				return {};
			result.score_ += delta <= 2000 ? 20 : delta <= 5000 ? 15 : delta <= 10000 ? 10 : 5;
		} else if(*query.duration_ < Milliseconds{90000})
			return {};
	}
	if(!candidate.synced_.empty() || !candidate.resource_id_.empty())
		result.score_ += 8;
	else if(!candidate.plain_.empty())
		result.score_ += 2;
	if(candidate.is_instrumental_)
		result.score_ += 4;
	result.is_accepted_ = result.score_ >=
		((!candidate.synced_.empty() || candidate.is_instrumental_ || !candidate.resource_id_.empty()) ? 72 : 68);
	result.is_high_confidence_ = result.is_accepted_ && result.score_ >= 92 && title >= 0.92 &&
		(trim(query.artist_).empty() || artist >= 0.9) && (!query.duration_ || candidate.duration_);
	return result;
}

string decode_base64(string_view encoded) {
	string output{};
	output.reserve(encoded.size() / 4 * 3);
	unsigned bits{}, count{};
	bool     has_padding{};
	for(const char raw : encoded) {
		const auto c{static_cast<unsigned char>(raw)};
		if(c == ' ' || c == '\r' || c == '\n' || c == '\t')
			continue;
		if(c == '=') {
			has_padding = true;
			continue;
		}
		if(has_padding)
			return {};
		unsigned value{};
		if(c >= 'A' && c <= 'Z')
			value = c - 'A';
		else if(c >= 'a' && c <= 'z')
			value = c - 'a' + 26;
		else if(c >= '0' && c <= '9')
			value = c - '0' + 52;
		else if(c == '+')
			value = 62;
		else if(c == '/')
			value = 63;
		else
			return {};
		bits = (bits << 6) | value;
		count += 6;
		if(count >= 8) {
			count -= 8;
			output += static_cast<char>((bits >> count) & 255);
		}
	}
	if(count == 6)
		return {};
	return output;
}

expected<vector<Candidate>, string> parse_candidates(string_view payload, Provider provider, Stage stage) {
	vector<Candidate> result{};
	if(stage == Stage::LrcxText) {
		Candidate candidate{};
		candidate.provider_   = provider;
		candidate.synced_     = payload;
		candidate.is_trusted_ = false;
		result.push_back(std::move(candidate));
		return result;
	}
	if(trim(payload).empty())
		return unexpected("Empty JSON response");
	using namespace Responses;
	switch(stage) {
		case Stage::LrclibGet:
		case Stage::LrclibSearch:
			return direct_response<Lrclib>(payload, provider, [](const auto& row) {
				Candidate candidate{};
				candidate.title_           = first({&row.trackName, &row.name});
				candidate.artist_          = first({&row.artistName, &row.artist});
				candidate.album_           = first({&row.albumName, &row.album});
				candidate.duration_        = duration(numeric(row.duration), 1000);
				candidate.synced_          = scalar_string(row.syncedLyrics);
				candidate.plain_           = scalar_string(row.plainLyrics);
				candidate.is_instrumental_ = is_truthy(row.instrumental);
				return candidate;
			});
		case Stage::LrcxJson:
			return direct_response<Lrcx>(payload, provider, [](const auto& row) {
				Candidate candidate{};
				candidate.title_  = first({&row.title, &row.trackName, &row.name});
				candidate.artist_ = first({&row.artist, &row.artistName});
				candidate.album_  = first({&row.album, &row.albumName});
				const auto seconds{numeric(row.duration)};
				candidate.duration_ = duration(seconds, seconds < 10000 ? 1000 : 1);
				candidate.synced_   = first({&row.lyrics, &row.lyric, &row.lrc, &row.syncedLyrics});
				if(candidate.synced_.empty())
					candidate.synced_ = ttml_to_lrc(scalar_string(row.lrc_ttml));
				candidate.plain_ = scalar_string(row.plainLyrics);
				return candidate;
			});
		case Stage::NeteaseSearch:
			return response<Object<NeteaseSearch>>(payload, provider, [](const auto& document, auto append) {
				const auto& songs{value<NeteaseResult>(value<NeteaseSearch>(document).result).songs};
				rows(
					songs,
					[](const auto& row) {
						Candidate candidate{};
						candidate.title_  = scalar_string(row.name);
						candidate.artist_ = artists(row.artists);
						if(candidate.artist_.empty())
							candidate.artist_ = artists(row.ar);
						candidate.album_ = scalar_string(value<Album>(row.album).name);
						if(candidate.album_.empty())
							candidate.album_ = scalar_string(value<Album>(row.al).name);
						candidate.duration_ = duration(numeric(row.duration));
						if(!candidate.duration_)
							candidate.duration_ = duration(numeric(row.dt));
						candidate.resource_id_ = scalar_string(row.id);
						return candidate;
					},
					append);
			});
		case Stage::NeteaseLyric:
			return direct_response<NeteaseLyric>(payload, provider, [](const auto& row) {
				Candidate candidate{};
				candidate.synced_ = scalar_string(value<Lyric>(row.lrc).lyric);
				candidate.plain_  = scalar_string(value<Lyric>(row.tlyric).lyric);
				return candidate;
			});
		case Stage::QqSearch:
			return response<Object<QqSearch>>(payload, provider, [](const auto& document, auto append) {
				const auto& songs{value<QqSongs>(value<QqData>(value<QqSearch>(document).data).song).list};
				rows(
					songs,
					[](const auto& row) {
						Candidate candidate{};
						candidate.title_       = first({&row.songname, &row.title});
						candidate.artist_      = artists(row.singer);
						candidate.album_       = scalar_string(row.albumname);
						candidate.duration_    = duration(numeric(row.interval), 1000);
						candidate.resource_id_ = first({&row.songmid, &row.mid});
						return candidate;
					},
					append);
			});
		case Stage::QqLyric:
			return direct_response<QqLyric>(payload, provider, [](const auto& row) {
				Candidate candidate{};
				candidate.synced_ = scalar_string(row.lyric);
				if(candidate.synced_.find('[') == candidate.synced_.npos) {
					if(auto decoded = decode_base64(candidate.synced_); !decoded.empty() && is_valid_utf8(decoded))
						candidate.synced_ = std::move(decoded);
				}
				return candidate;
			});
		case Stage::KugouSearch:
			return response<Object<KugouSearch>>(payload, provider, [](const auto& document, auto append) {
				const auto& songs{value<KugouData>(value<KugouSearch>(document).data).lists};
				rows(
					songs,
					[](const auto& row) {
						Candidate candidate{};
						candidate.title_       = clean_text(first({&row.SongName, &row.FileName}));
						candidate.artist_      = clean_text(scalar_string(row.SingerName));
						candidate.album_       = clean_text(scalar_string(row.AlbumName));
						candidate.duration_    = duration(numeric(row.Duration), 1000);
						candidate.resource_id_ = first({&row.FileHash, &row.Hash});
						return candidate;
					},
					append);
			});
		case Stage::KugouLyricSearch:
			return response<Object<KugouLyricSearch>>(payload, provider, [](const auto& document, auto append) {
				rows(
					value<KugouLyricSearch>(document).candidates,
					[](const auto& row) {
						Candidate candidate{};
						candidate.resource_id_ = scalar_string(row.id);
						candidate.access_key_  = scalar_string(row.accesskey);
						return candidate;
					},
					append);
			});
		case Stage::KugouDownload:
			return direct_response<KugouDownload>(payload, provider, [](const auto& row) {
				Candidate candidate{};
				candidate.synced_ = scalar_string(row.content);
				if(auto decoded = decode_base64(candidate.synced_); !decoded.empty())
					candidate.synced_ = std::move(decoded);
				return candidate;
			});
		case Stage::MusixmatchSubtitle:
		case Stage::MusixmatchPlain:
			return direct_response<Musixmatch>(payload, provider, [](const auto& row) {
				const auto& body{value<MusixmatchBody>(value<MusixmatchMessage>(row.message).body)};
				Candidate   candidate{};
				candidate.synced_ = scalar_string(value<Subtitle>(body.subtitle).subtitle_body);
				candidate.plain_  = scalar_string(value<PlainLyrics>(body.lyrics).lyrics_body);
				return candidate;
			});
		case Stage::LrcxText:
			break;
	}
	return result;
}

HttpRequest
provider_request(uint64_t id, Stage stage, const TrackQuery& query, const Candidate& candidate, string_view api_key) {
	HttpRequest request{id, {}, {}};
	auto&       url{request.url_};
	const auto  add{[&](string_view key, string_view value) { parameter(url, key, value); }};
	const auto  seconds{query.duration_ ? to_string(max<int64_t>(1, query.duration_->count() / 1000)) : ""};
	switch(stage) {
		case Stage::LrclibGet:
		case Stage::LrclibSearch:
			url = stage == Stage::LrclibGet ? "https://lrclib.net/api/get" : "https://lrclib.net/api/search";
			add("track_name", query.title_);
			add("artist_name", query.artist_);
			if(stage == Stage::LrclibGet) {
				if(!query.album_.empty())
					add("album_name", query.album_);
				if(!seconds.empty())
					add("duration", seconds);
			}
			break;
		case Stage::LrcxJson:
		case Stage::LrcxText:
			url = stage == Stage::LrcxJson ? "https://api.lrc.cx/jsonapi" : "https://api.lrc.cx/lyrics";
			add("title", query.title_);
			add("artist", query.artist_);
			if(stage == Stage::LrcxJson) {
				if(!query.album_.empty())
					add("album", query.album_);
				if(!seconds.empty())
					add("duration", seconds);
			}
			break;
		case Stage::NeteaseSearch:
			url = "https://music.163.com/api/search/get";
			add("s", query.title_ + ' ' + query.artist_);
			add("type", "1");
			add("limit", "5");
			add("offset", "0");
			break;
		case Stage::NeteaseLyric:
			url = "https://music.163.com/api/song/lyric";
			add("os", "pc");
			add("id", candidate.resource_id_);
			add("lv", "-1");
			add("tv", "-1");
			break;
		case Stage::QqSearch:
			url = "https://c.y.qq.com/soso/fcgi-bin/client_search_cp";
			add("format", "json");
			add("p", "1");
			add("n", "5");
			add("w", query.title_ + ' ' + query.artist_);
			break;
		case Stage::QqLyric:
			url = "https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg";
			add("songmid", candidate.resource_id_);
			add("format", "json");
			add("nobase64", "1");
			break;
		case Stage::KugouSearch:
			url = "https://songsearch.kugou.com/song_search_v2";
			add("keyword", query.title_ + ' ' + query.artist_);
			add("page", "1");
			add("pagesize", "5");
			break;
		case Stage::KugouLyricSearch:
			url = "https://lyrics.kugou.com/search";
			add("ver", "1");
			add("man", "yes");
			add("client", "pc");
			add("keyword", candidate.title_ + ' ' + candidate.artist_);
			add("duration", to_string(candidate.duration_ ? candidate.duration_->count() : 1));
			add("hash", candidate.resource_id_);
			break;
		case Stage::KugouDownload:
			url = "https://lyrics.kugou.com/download";
			add("ver", "1");
			add("client", "pc");
			add("id", candidate.resource_id_);
			add("accesskey", candidate.access_key_);
			add("fmt", "lrc");
			add("charset", "utf8");
			break;
		case Stage::MusixmatchSubtitle:
		case Stage::MusixmatchPlain:
			url = stage == Stage::MusixmatchSubtitle ? "https://api.musixmatch.com/ws/1.1/matcher.subtitle.get"
													 : "https://api.musixmatch.com/ws/1.1/matcher.lyrics.get";
			add("q_track", query.title_);
			add("q_artist", query.artist_);
			add("apikey", api_key);
			break;
	}
	if(stage == Stage::NeteaseSearch || stage == Stage::NeteaseLyric)
		request.referer_ = "https://music.163.com/";
	if(stage == Stage::QqSearch || stage == Stage::QqLyric)
		request.referer_ = "https://y.qq.com/";
	if(stage == Stage::KugouSearch || stage == Stage::KugouLyricSearch || stage == Stage::KugouDownload)
		request.referer_ = "https://www.kugou.com/";
	return request;
}

}
