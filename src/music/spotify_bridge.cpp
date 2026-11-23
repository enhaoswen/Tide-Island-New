#include "music/spotify_bridge.hpp"

#include "backend/timer.hpp"
#include "utils/log.hpp"

#include <glaze/json/read.hpp>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>

using namespace std;
using namespace std::chrono;

namespace SpotifyBridge::wire {

struct ConfigFile {
    int    version{};
    int    port{};
    string token{};
};

struct Message {
    string         type{};
    int            version{};
    string         token{};
    string         id{};
    string         uri{};
    optional<bool> liked{};
    optional<bool> ok{};
};

}

namespace {

using SpotifyBridge::wire::ConfigFile;
using SpotifyBridge::wire::Message;

constexpr size_t      kMaxMessage     = 16 * 1024;
constexpr size_t      kMaxHandshake   = 8 * 1024;
constexpr size_t      kMaxConnections = 4; // the client plus a few pending handshakes
constexpr auto        kAuthTimeout    = 3s;
constexpr auto        kRequestTimeout = 10s;
constexpr string_view kSpotifyOrigin  = "https://xpui.app.spotify.com";

struct Conn {
    int                      fd = -1;
    bool                     upgraded{};
    bool                     authed{};
    steady_clock::time_point opened{};
    string                   rx{};
};

string                    config_path{};
optional<struct timespec> config_mtime{}; // nullopt: file absent at last check
bool                      config_checked{};

int      listen_fd = -1;
uint16_t port{};
string   token{};

vector<Conn> conns{};
int          client_fd = -1;

SpotifyBridge::State st{};
string               request_id{}, request_uri{};
bool                 request_liked{};
uint64_t             request_counter{};
Timer::EventHandle   request_timer{}, auth_timer{};

void (*listener)() = nullptr;

void notify() {
    if (listener)
        listener();
}

[[nodiscard]] bool is_track_uri(string_view uri) {
    constexpr string_view prefix = "spotify:track:";
    if (uri.size() != prefix.size() + 22 || !uri.starts_with(prefix))
        return false;
    return all_of(uri.begin() + prefix.size(), uri.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    });
}

[[nodiscard]] array<uint8_t, 20> sha1(string_view data) {
    uint32_t       h[5]{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    string         msg{data};
    const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56)
        msg.push_back('\0');
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<char>(bits >> (i * 8)));

    const auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const uint8_t*>(msg.data() + chunk + i * 4);
            w[i]          = uint32_t{p[0]} << 24 | uint32_t{p[1]} << 16 | uint32_t{p[2]} << 8 | p[3];
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)
                f = (b & c) | (~b & d), k = 0x5A827999;
            else if (i < 40)
                f = b ^ c ^ d, k = 0x6ED9EBA1;
            else if (i < 60)
                f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
            else
                f = b ^ c ^ d, k = 0xCA62C1D6;
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e                = d, d = c, c = rol(b, 30), b = a, a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    }
    array<uint8_t, 20> out{};
    for (int i = 0; i < 20; ++i)
        out[static_cast<size_t>(i)] = static_cast<uint8_t>(h[i / 4] >> (24 - (i % 4) * 8));
    return out;
}

[[nodiscard]] string base64(span<const uint8_t> in) {
    constexpr string_view table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string                out;
    for (size_t i = 0; i < in.size(); i += 3) {
        const uint32_t n = uint32_t{in[i]} << 16 | (i + 1 < in.size() ? uint32_t{in[i + 1]} << 8 : 0)
            | (i + 2 < in.size() ? in[i + 2] : 0);
        out.push_back(table[n >> 18 & 63]);
        out.push_back(table[n >> 12 & 63]);
        out.push_back(i + 1 < in.size() ? table[n >> 6 & 63] : '=');
        out.push_back(i + 2 < in.size() ? table[n & 63] : '=');
    }
    return out;
}

bool send_all(int fd, string_view bytes) {
    const ssize_t n = send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n == static_cast<ssize_t>(bytes.size()))
        return true;
    shutdown(fd, SHUT_RDWR);
    return false;
}

bool send_frame(int fd, uint8_t opcode, string_view payload) {
    string frame;
    frame.push_back(static_cast<char>(0x80 | opcode));
    if (payload.size() < 126)
        frame.push_back(static_cast<char>(payload.size()));
    else {
        frame.push_back(126);
        frame.push_back(static_cast<char>(payload.size() >> 8));
        frame.push_back(static_cast<char>(payload.size() & 0xff));
    }
    frame += payload;
    return send_all(fd, frame);
}

void clear_request() {
    Timer::cancel(request_timer);
    request_timer = {};
    request_id.clear();
    request_uri.clear();
    st.busy = false;
}

void reset_state() {
    clear_request();
    st = {};
}

void close_conn(Conn& c) {
    if (c.fd < 0)
        return;
    if (c.fd == client_fd) {
        client_fd = -1;
        reset_state();
        notify();
    }
    close(c.fd);
    c.fd = -1;
}

void close_all() {
    for (Conn& c : conns)
        close_conn(c);
    conns.clear();
    if (listen_fd >= 0)
        close(listen_fd);
    listen_fd = -1;
    Timer::cancel(auth_timer);
    auth_timer = {};
}

void on_auth_timeout();

void arm_auth_timer() {
    if (Timer::is_pending(auth_timer))
        return;
    optional<steady_clock::time_point> earliest;
    for (const Conn& c : conns)
        if (c.fd >= 0 && !c.authed && (!earliest || c.opened < *earliest))
            earliest = c.opened;
    if (!earliest)
        return;
    const auto left = max(duration_cast<microseconds>(*earliest + kAuthTimeout - steady_clock::now()), microseconds{1});
    auth_timer      = Timer::push(left, on_auth_timeout, 0);
}

void on_auth_timeout() {
    auth_timer     = {};
    const auto now = steady_clock::now();
    for (Conn& c : conns)
        if (c.fd >= 0 && !c.authed && now - c.opened >= kAuthTimeout)
            close_conn(c);
    erase_if(conns, [](const Conn& c) { return c.fd < 0; });
    arm_auth_timer();
}

void on_request_timeout() {
    request_timer = {};
    clear_request();
    st.known = false; // Spotify never confirmed; don't guess
    notify();
}

[[nodiscard]] bool token_matches(string_view given) {
    if (given.size() != token.size())
        return false;
    unsigned diff = 0;
    for (size_t i = 0; i < given.size(); ++i)
        diff |= static_cast<unsigned char>(given[i] ^ token[i]);
    return diff == 0;
}

void apply_state(const Message& m) {
    const string uri   = is_track_uri(m.uri) ? m.uri : string{};
    const bool   known = !uri.empty() && m.liked.has_value();
    const bool   liked = known && *m.liked;
    if (st.track_uri == uri && st.known == known && st.liked == liked)
        return;
    st.track_uri = uri;
    st.known     = known;
    st.liked     = liked;
    notify();
}

void apply_result(const Message& m) {
    if (!st.busy || m.id != request_id || m.uri != request_uri)
        return;
    const bool success = m.ok.value_or(false) && m.liked.has_value() && *m.liked == request_liked;
    if (success && st.track_uri == request_uri) {
        st.liked = request_liked;
        st.known = true;
    }
    if (!success)
        Log::logger(Log::Warning, "Spotify did not update the favorite");
    clear_request();
    notify();
}

bool handle_text(Conn& c, string_view text) {
    Message m{};
    if (glz::read<glz::opts{.error_on_unknown_keys = false}>(m, text))
        return c.authed; // malformed: fatal before authentication, ignored after

    if (!c.authed) {
        if (m.type != "hello" || m.version != 1 || token.empty() || !token_matches(m.token) || client_fd >= 0)
            return false;
        c.authed     = true;
        client_fd    = c.fd;
        st.connected = true;
        notify();
        return send_frame(c.fd, 0x1, R"({"type":"welcome","version":1})");
    }
    if (c.fd != client_fd)
        return true;
    if (m.type == "state")
        apply_state(m);
    else if (m.type == "result")
        apply_result(m);
    return true;
}

[[nodiscard]] string_view header_value(string_view head, string_view name) {
    size_t pos = head.find("\r\n");
    while (pos != string_view::npos && pos + 2 < head.size()) {
        const size_t      start = pos + 2;
        const size_t      end   = head.find("\r\n", start);
        const string_view line  = head.substr(start, end == string_view::npos ? string_view::npos : end - start);
        const size_t      colon = line.find(':');
        if (colon == name.size()
            && equal(name.begin(), name.end(), line.begin(), [](char a, char b) { return tolower(a) == tolower(b); })) {
            string_view v = line.substr(colon + 1);
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
                v.remove_prefix(1);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t'))
                v.remove_suffix(1);
            return v;
        }
        pos = end;
    }
    return {};
}

bool handle_handshake(Conn& c) {
    const size_t end = c.rx.find("\r\n\r\n");
    if (end == string::npos)
        return c.rx.size() <= kMaxHandshake;
    const string_view head{c.rx.data(), end + 2};
    const string_view key    = header_value(head, "Sec-WebSocket-Key");
    const string_view origin = header_value(head, "Origin");
    if (!head.starts_with("GET ") || key.empty() || key.size() > 64
        || (!origin.empty() && origin != kSpotifyOrigin)) {
        send_all(c.fd, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
        return false;
    }
    const auto   digest = sha1(string{key} + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    const string reply  = format(
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Accept: {}\r\n\r\n",
        base64(digest));
    c.rx.erase(0, end + 4);
    c.upgraded = true;
    return send_all(c.fd, reply);
}

bool handle_frames(Conn& c) {
    for (;;) {
        const auto* p = reinterpret_cast<const uint8_t*>(c.rx.data());
        if (c.rx.size() < 2)
            return true;
        const bool    fin    = p[0] & 0x80;
        const uint8_t opcode = p[0] & 0x0f;
        if (!(p[1] & 0x80))
            return false; // clients must mask
        uint64_t len = p[1] & 0x7f;
        size_t   hdr = 2;
        if (len == 126) {
            if (c.rx.size() < 4)
                return true;
            len = uint64_t{p[2]} << 8 | p[3];
            hdr = 4;
        }
        else if (len == 127)
            return false; // > 64 KiB, far above anything the extension sends
        if (len > kMaxMessage)
            return false;
        if (c.rx.size() < hdr + 4 + len)
            return true;

        string payload = c.rx.substr(hdr + 4, len);
        for (size_t i = 0; i < payload.size(); ++i)
            payload[i] = static_cast<char>(payload[i] ^ c.rx[hdr + i % 4]);
        c.rx.erase(0, hdr + 4 + len);

        switch (opcode) {
        case 0x1: // text; the extension never fragments its tiny JSON messages
            if (!fin || !handle_text(c, payload))
                return false;
            break;
        case 0x8: // close
            send_frame(c.fd, 0x8, {});
            return false;
        case 0x9: // ping
            if (!send_frame(c.fd, 0xA, payload))
                return false;
            break;
        case 0xA: // pong
            break;
        default:
            return false;
        }
    }
}

bool handle_readable(Conn& c) {
    char buf[4096];
    for (;;) {
        const ssize_t n = recv(c.fd, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            c.rx.append(buf, static_cast<size_t>(n));
            if (c.rx.size() > kMaxMessage + kMaxHandshake)
                return false;
            continue;
        }
        if (n == 0)
            return false;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        return false;
    }
    if (!c.upgraded && !handle_handshake(c))
        return false;
    return !c.upgraded || handle_frames(c);
}

void accept_pending() {
    for (;;) {
        const int fd = accept4(listen_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
            return; // EAGAIN, or a transient error retried on the next wakeup
        if (conns.size() >= kMaxConnections) {
            close(fd);
            continue;
        }
        conns.push_back(Conn{.fd = fd, .upgraded = false, .authed = false, .opened = steady_clock::now(), .rx = {}});
    }
}

[[nodiscard]] string find_config_path() {
    if (const char* xdg = getenv("XDG_CONFIG_HOME"); xdg && *xdg == '/')
        return string{xdg} + "/tide-island/spotify-bridge.json";
    if (const char* home = getenv("HOME"); home && *home)
        return string{home} + "/.config/tide-island/spotify-bridge.json";
    return {};
}

[[nodiscard]] bool valid_token(string_view t) {
    return t.size() == 64 && all_of(t.begin(), t.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

void start_listening(uint16_t p) {
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return;
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(p);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0 || listen(fd, 4) != 0) {
        Log::logger(Log::Warning, "Spotify favorites: cannot listen on 127.0.0.1:{} ({})", p, strerror(errno));
        close(fd);
        return;
    }
    listen_fd = fd;
    Log::logger(Log::Debug, "Spotify favorites bridge listening on 127.0.0.1:{}", p);
}

void apply_config() {
    ConfigFile cfg{};
    bool       valid = false;
    if (config_mtime) {
        string buffer;
        valid = !glz::read_file_json<glz::opts{.error_on_unknown_keys = false}>(cfg, config_path, buffer)
            && cfg.version == 1 && valid_token(cfg.token) && cfg.port >= 1024 && cfg.port <= 65535;
        if (!valid)
            Log::logger(Log::Warning, "Spotify favorites setup is invalid: {}", config_path);
    }
    const uint16_t next_port  = valid ? static_cast<uint16_t>(cfg.port) : 0;
    const string   next_token = valid ? cfg.token : string{};
    if (next_port == port && next_token == token && (listen_fd >= 0) == valid)
        return;

    close_all();
    reset_state();
    port  = next_port;
    token = next_token;
    if (valid)
        start_listening(port);
    notify();
}

}

void SpotifyBridge::init() {
    config_path = find_config_path();
    reload();
}

void SpotifyBridge::reload() {
    if (config_path.empty())
        return;
    struct stat               info{};
    optional<struct timespec> mtime{};
    if (stat(config_path.c_str(), &info) == 0)
        mtime = info.st_mtim;
    const bool same = config_checked && mtime.has_value() == config_mtime.has_value()
        && (!mtime || (mtime->tv_sec == config_mtime->tv_sec && mtime->tv_nsec == config_mtime->tv_nsec));
    if (same)
        return;
    config_checked = true;
    config_mtime   = mtime;
    apply_config();
}

void SpotifyBridge::shutdown() {
    listener = nullptr;
    close_all();
    reset_state();
}

const SpotifyBridge::State& SpotifyBridge::state() { return st; }

bool SpotifyBridge::set_favorite(string_view uri, bool liked) {
    if (client_fd < 0 || st.busy || !st.known || uri != st.track_uri || !is_track_uri(uri))
        return false;
    request_id       = to_string(++request_counter);
    request_uri      = uri;
    request_liked    = liked;
    // uri is [A-Za-z0-9:] and id is digits: nothing to escape.
    const string msg = format(R"({{"type":"setFavorite","id":"{}","uri":"{}","liked":{}}})", request_id, uri, liked);
    if (!send_frame(client_fd, 0x1, msg)) {
        request_id.clear();
        request_uri.clear();
        return false;
    }
    st.busy       = true;
    request_timer = Timer::push(duration_cast<microseconds>(kRequestTimeout), on_request_timeout, 0);
    notify();
    return true;
}

void SpotifyBridge::set_listener(void (*l)()) { listener = l; }

void SpotifyBridge::append_poll_fds(vector<pollfd>& out) {
    if (listen_fd < 0)
        return;
    out.push_back({.fd = listen_fd, .events = POLLIN, .revents = 0});
    for (const Conn& c : conns)
        out.push_back({.fd = c.fd, .events = POLLIN, .revents = 0});
}

void SpotifyBridge::process(span<const pollfd> ready) {
    bool accept_ready = false;
    for (const pollfd& p : ready) {
        if (!p.revents)
            continue;
        if (p.fd == listen_fd) {
            accept_ready = true;
            continue;
        }
        const auto it = find_if(conns.begin(), conns.end(), [&](const Conn& c) { return c.fd == p.fd; });
        if (it == conns.end())
            continue;
        if ((p.revents & (POLLERR | POLLNVAL)) || !handle_readable(*it))
            close_conn(*it);
    }
    erase_if(conns, [](const Conn& c) { return c.fd < 0; });
    if (accept_ready)
        accept_pending();
    arm_auth_timer();
}

bool SpotifyBridge::is_spotify_service(string_view service) {
    // (^|.)spotify([.-]|$), case-insensitive, like the original SpotifyTrack.js
    for (size_t i = 0; i + 7 <= service.size(); ++i) {
        const bool word = equal(service.begin() + static_cast<ptrdiff_t>(i), service.begin() + static_cast<ptrdiff_t>(i + 7),
            "spotify", [](char a, char b) { return tolower(a) == b; });
        if (!word)
            continue;
        const bool before = i == 0 || service[i - 1] == '.';
        const bool after  = i + 7 == service.size() || service[i + 7] == '.' || service[i + 7] == '-';
        if (before && after)
            return true;
    }
    return false;
}

string SpotifyBridge::track_uri_from_mpris(string_view service, string_view track_id, string_view media_url) {
    if (!is_spotify_service(service))
        return {};
    const auto from = [](string_view s) -> string {
        if (is_track_uri(s))
            return string{s};
        constexpr string_view web = "https://open.spotify.com/";
        if (s.starts_with(web)) {
            s.remove_prefix(web.size());
            if (s.starts_with("intl-"))
                s.remove_prefix(min(s.size(), s.find('/') + 1));
            if (!s.starts_with("track/"))
                return {};
            s.remove_prefix(6);
            s = s.substr(0, s.find_first_of("?#"));
        }
        else if (s.starts_with("/com/spotify/track/"))
            s.remove_prefix(19);
        else
            return {};
        string uri = "spotify:track:" + string{s};
        return is_track_uri(uri) ? uri : string{};
    };
    if (string uri = from(media_url); !uri.empty())
        return uri;
    return from(track_id);
}