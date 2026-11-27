#include "music/media.hpp"

#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "lyricsmpris/detail/http.hpp"
#include "utils/log.hpp"

using namespace std;
using namespace std::chrono;

namespace {

constexpr size_t k_max_artwork_bytes{16u * 1024u * 1024u};

optional<LyricsMpris::Client>         client{};
unique_ptr<LyricsMpris::Detail::Http> http{};

void (*listener)(Media::Update){};

string                  artwork_url{};
optional<Image::Pixels> artwork_pixels{}; // downscaled; encoded bytes are dropped after decoding
uint64_t                artwork_request{};
uint64_t                next_request_id{1};

[[nodiscard]] int hex_value(char c) {
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

[[nodiscard]] string file_url_to_path(string_view url) {
	constexpr string_view k_prefix{"file://"};
	url.remove_prefix(k_prefix.size());
	if(url.starts_with("localhost/"))
		url.remove_prefix(9);
	if(!url.starts_with('/'))
		return {};

	string out;
	out.reserve(url.size());
	for(size_t i = 0; i < url.size(); ++i) {
		if(url[i] != '%') {
			out.push_back(url[i]);
			continue;
		}
		if(i + 2 >= url.size())
			return {};
		const int hi{hex_value(url[i + 1])};
		const int lo{hex_value(url[i + 2])};
		if(hi < 0 || lo < 0)
			return {};
		out.push_back(static_cast<char>(hi * 16 + lo));
		i += 2;
	}
	return out;
}

[[nodiscard]] vector<uint8_t> read_file(const string& path) {
	ifstream in{path, ios::binary | ios::ate};
	if(!in)
		return {};
	const auto size{in.tellg()};
	if(size <= 0 || static_cast<size_t>(size) > k_max_artwork_bytes)
		return {};
	vector<uint8_t> bytes(static_cast<size_t>(size));
	in.seekg(0);
	if(!in.read(reinterpret_cast<char*>(bytes.data()), size))
		return {};
	return bytes;
}

[[nodiscard]] bool ensure_http() {
	if(http)
		return true;
	LyricsMpris::Options options{};
	options.max_response_bytes_ = k_max_artwork_bytes;
	options.max_connections_    = 1;
	auto created{LyricsMpris::Detail::Http::create(options)};
	if(!created) {
		Log::logger(Log::LogLevel::Warning, "Album art download unavailable: {}", created.error().message_);
		return false;
	}
	http = std::move(*created);
	return true;
}

// Returns whether artwork_pixels changed synchronously.
bool fetch_artwork(const string& url) {
	if(url == artwork_url)
		return false;

	artwork_url     = url;
	artwork_request = 0;
	http.reset(); // drops any in-flight download together with its connection

	const bool has_artwork{artwork_pixels.has_value()};
	artwork_pixels.reset();

	if(url.starts_with("file://")) {
		const string path{file_url_to_path(url)};
		if(!path.empty())
			artwork_pixels = Image::decode_square(read_file(path), Media::k_artwork_side);
		if(!artwork_pixels)
			Log::logger(Log::LogLevel::Warning, "Failed to load album art: {}", url);
		return true;
	}

	if((url.starts_with("https://") || url.starts_with("http://")) && ensure_http()) {
		if(const uint64_t id = next_request_id++; http->enqueue({.id_ = id, .url_ = url, .referer_ = {}}))
			artwork_request = id;
		else
			http.reset();
	}
	return has_artwork;
}

bool process_http(span<const pollfd> ready, steady_clock::time_point now) {
	if(!http)
		return false;

	auto responses{http->process(ready, now)};
	if(!responses) {
		Log::logger(Log::LogLevel::Warning, "Album art download failed: {}", responses.error().message_);
		http.reset();
		artwork_request = 0;
		return false;
	}

	bool has_changed{};
	for(auto& r : *responses) {
		if(r.id_ != artwork_request)
			continue;
		artwork_request = 0;
		if(r.error_ != CURLE_OK || r.status_ != 200 || r.body_.empty()) {
			Log::logger(Log::LogLevel::Warning, "Album art download failed (status {}): {}", r.status_, artwork_url);
			continue;
		}
		artwork_pixels = Image::decode_square({reinterpret_cast<const uint8_t*>(r.body_.data()), r.body_.size()},
			Media::k_artwork_side);
		has_changed    = true;
	}
	// One cover per track: release curl + the TLS connection (~1 MiB) once idle.
	if(!http->is_busy())
		http.reset();
	return has_changed;
}

void notify(Media::Update u) {
	if(u != Media::Update::None && listener)
		listener(u);
}

}

namespace Media {
void init() {
	if(client)
		return;
	auto created{LyricsMpris::Client::create()};
	if(!created) {
		Log::logger(Log::LogLevel::Warning, "MPRIS unavailable: {}", created.error().message_);
		return;
	}
	client.emplace(std::move(*created));
}

void shutdown() {
	listener = nullptr;
	http.reset();
	client.reset();
	artwork_url.clear();
	artwork_pixels.reset();
	artwork_request = 0;
}

bool is_available() {
	return client.has_value();
}

const LyricsMpris::State* get_state() {
	return client ? &client->get_state() : nullptr;
}

LyricsMpris::Milliseconds get_position() {
	return client ? client->position() : LyricsMpris::Milliseconds{};
}

const Image::Pixels* get_artwork() {
	return artwork_pixels ? &*artwork_pixels : nullptr;
}

namespace {

void report(const char* what, const expected<void, LyricsMpris::Error>& r) {
	if(!r)
		Log::logger(Log::LogLevel::Warning, "Media {} failed: {}", what, r.error().message_);
}

}

void resync_position() {
	if(client)
		client->resync_position();
}

void play_pause() {
	if(!client)
		return;
	const auto& caps{client->get_state().capabilities_};
	if(caps.can_play_ && caps.can_pause_)
		report("play/pause", client->play_pause());
	else if(client->get_state().playback_ == LyricsMpris::Playback::Playing)
		report("pause", client->pause());
	else
		report("play", client->play());
}

void next() {
	if(client)
		report("next", client->next());
}

void previous() {
	if(client)
		report("previous", client->previous());
}

void set_listener(void (*l)(Update)) {
	listener = l;
}

void append_poll_fds(vector<pollfd>& out) {
	if(!client)
		return;
	const auto fds{client->get_poll_fds()};
	out.insert(out.end(), fds.begin(), fds.end());
	if(http) {
		const auto h{http->get_poll_fds()};
		out.insert(out.end(), h.begin(), h.end());
	}
}

optional<steady_clock::time_point> next_deadline() {
	if(!client)
		return nullopt;
	auto deadline{client->get_next_deadline()};
	if(http) {
		if(const auto h = http->get_deadline(); h && (!deadline || *h < *deadline))
			deadline = h;
	}
	return deadline;
}

void process(span<const pollfd> ready) {
	if(!client)
		return;

	const auto now{steady_clock::now()};
	const auto changes{client->process(ready, now)};
	if(!changes) {
		Log::logger(Log::LogLevel::Error, "MPRIS client failed, media disabled: {}", changes.error().message_);
		shutdown();
		return;
	}

	using LyricsMpris::Changes;
	constexpr auto k_state_changes = Changes::Player | Changes::Track | Changes::Playback | Changes::Position |
		Changes::Capabilities | Changes::Error;
	constexpr auto k_lyric_changes{Changes::Lyrics | Changes::Line | Changes::Search};

	Update update{Update::None};
	if(LyricsMpris::has_flag(*changes, k_state_changes))
		update = update | Update::State;
	if(LyricsMpris::has_flag(*changes, k_lyric_changes))
		update = update | Update::Lyrics;

	if(const auto& s = client->get_state(); fetch_artwork(s.has_player_ ? s.artwork_url_ : string{}))
		update = update | Update::Artwork;
	if(process_http(ready, now))
		update = update | Update::Artwork;

	notify(update);
}

}
