#include "backend/media.hpp"

#include "lyricsmpris/detail/Http.hpp"
#include "utils/log.hpp"

#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

using namespace std;
using namespace std::chrono;

namespace {

constexpr size_t kMaxArtworkBytes = 16u * 1024u * 1024u;

optional<lyricsmpris::Client>        client{};
unique_ptr<lyricsmpris::detail::Http> http{};

void (*listener)(Media::Update) = nullptr;

string          artwork_url{};
vector<uint8_t> artwork_bytes{};
uint64_t        artwork_request{};
uint64_t        next_request_id{1};

[[nodiscard]] int hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

[[nodiscard]] string file_url_to_path(string_view url) {
    constexpr string_view prefix = "file://";
    url.remove_prefix(prefix.size());
    if (url.starts_with("localhost/"))
        url.remove_prefix(9);
    if (!url.starts_with('/'))
        return {};

    string out;
    out.reserve(url.size());
    for (size_t i = 0; i < url.size(); ++i) {
        if (url[i] != '%') {
            out.push_back(url[i]);
            continue;
        }
        if (i + 2 >= url.size())
            return {};
        const int hi = hex_value(url[i + 1]);
        const int lo = hex_value(url[i + 2]);
        if (hi < 0 || lo < 0)
            return {};
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
    }
    return out;
}

[[nodiscard]] vector<uint8_t> read_file(const string& path) {
    ifstream in{path, ios::binary | ios::ate};
    if (!in)
        return {};
    const auto size = in.tellg();
    if (size <= 0 || static_cast<size_t>(size) > kMaxArtworkBytes)
        return {};
    vector<uint8_t> bytes(static_cast<size_t>(size));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size))
        return {};
    return bytes;
}

[[nodiscard]] bool ensure_http() {
    if (http)
        return true;
    lyricsmpris::Options options{};
    options.max_response_bytes = kMaxArtworkBytes;
    options.max_connections    = 1;
    auto created               = lyricsmpris::detail::Http::create(options);
    if (!created) {
        Log::logger(Log::Warning, "Album art download unavailable: {}", created.error().message);
        return false;
    }
    http = std::move(*created);
    return true;
}

bool fetch_artwork(const string& url) {
    if (url == artwork_url)
        return false;

    artwork_url     = url;
    artwork_request = 0;
    if (http)
        http->cancel();

    const bool had_bytes = !artwork_bytes.empty();
    artwork_bytes.clear();

    if (url.starts_with("file://")) {
        const string path = file_url_to_path(url);
        if (!path.empty())
            artwork_bytes = read_file(path);
        if (artwork_bytes.empty())
            Log::logger(Log::Warning, "Failed to read album art: {}", url);
        return true;
    }

    if ((url.starts_with("https://") || url.starts_with("http://")) && ensure_http()) {
        const uint64_t id = next_request_id++;
        if (http->enqueue({.id = id, .url = url, .referer = {}}))
            artwork_request = id;
    }
    return had_bytes;
}

bool process_http(span<const pollfd> ready, steady_clock::time_point now) {
    if (!http)
        return false;

    auto responses = http->process(ready, now);
    if (!responses) {
        Log::logger(Log::Warning, "Album art download failed: {}", responses.error().message);
        http.reset();
        artwork_request = 0;
        return false;
    }

    bool changed = false;
    for (auto& r : *responses) {
        if (r.id != artwork_request)
            continue;
        artwork_request = 0;
        if (r.error != CURLE_OK || r.status != 200 || r.body.empty()) {
            Log::logger(Log::Warning, "Album art download failed (status {}): {}", r.status, artwork_url);
            continue;
        }
        artwork_bytes.assign(r.body.begin(), r.body.end());
        changed = true;
    }
    return changed;
}

void notify(Media::Update u) {
    if (u != Media::Update::None && listener)
        listener(u);
}

}

void Media::init() {
    if (client)
        return;
    auto created = lyricsmpris::Client::create();
    if (!created) {
        Log::logger(Log::Warning, "MPRIS unavailable: {}", created.error().message);
        return;
    }
    client.emplace(std::move(*created));
}

void Media::shutdown() {
    listener = nullptr;
    http.reset();
    client.reset();
    artwork_url.clear();
    artwork_bytes.clear();
    artwork_request = 0;
}

bool Media::available() {
    return client.has_value();
}

const lyricsmpris::State* Media::state() {
    return client ? &client->state() : nullptr;
}

lyricsmpris::Milliseconds Media::position() {
    return client ? client->position() : lyricsmpris::Milliseconds{};
}

span<const uint8_t> Media::artwork() {
    return artwork_bytes;
}

namespace {

void report(const char* what, const expected<void, lyricsmpris::Error>& r) {
    if (!r)
        Log::logger(Log::Warning, "Media {} failed: {}", what, r.error().message);
}

}

void Media::play_pause() {
    if (!client)
        return;
    const auto& caps = client->state().capabilities;
    if (caps.play && caps.pause)
        report("play/pause", client->play_pause());
    else if (client->state().playback == lyricsmpris::Playback::Playing)
        report("pause", client->pause());
    else
        report("play", client->play());
}

void Media::next() {
    if (client)
        report("next", client->next());
}

void Media::previous() {
    if (client)
        report("previous", client->previous());
}

void Media::set_listener(void (*l)(Update)) {
    listener = l;
}

void Media::append_poll_fds(vector<pollfd>& out) {
    if (!client)
        return;
    const auto fds = client->poll_fds();
    out.insert(out.end(), fds.begin(), fds.end());
    if (http) {
        const auto h = http->poll_fds();
        out.insert(out.end(), h.begin(), h.end());
    }
}

optional<steady_clock::time_point> Media::next_deadline() {
    if (!client)
        return nullopt;
    auto deadline = client->next_deadline();
    if (http) {
        if (const auto h = http->deadline(); h && (!deadline || *h < *deadline))
            deadline = h;
    }
    return deadline;
}

bool Media::process(span<const pollfd> ready) {
    if (!client)
        return false;

    const auto now     = steady_clock::now();
    const auto changes = client->process(ready, now);
    if (!changes) {
        Log::logger(Log::Error, "MPRIS client failed, media disabled: {}", changes.error().message);
        shutdown();
        return true;
    }

    Update update = Update::None;
    if (*changes != lyricsmpris::Changes::None)
        update = Update::State;

    const auto& s = client->state();
    if (fetch_artwork(s.has_player ? s.artwork_url : string{}))
        update = update | Update::Artwork;
    if (process_http(ready, now))
        update = update | Update::Artwork;

    notify(update);
    return update != Update::None;
}
