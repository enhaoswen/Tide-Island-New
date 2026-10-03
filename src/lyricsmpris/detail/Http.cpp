#include "Http.hpp"

#include <algorithm>
#include <limits>

namespace lyricsmpris::detail {
namespace {

struct CurlRuntime {
    CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
    ~CurlRuntime() { if (result == CURLE_OK) curl_global_cleanup(); }
};

Error multi_error(CURLMcode code) {
    return {ErrorCode::Network, static_cast<int>(code), curl_multi_strerror(code)};
}

} // namespace

struct Http::Transfer {
    HttpRequest request;
    std::string body;
    CURL* easy = curl_easy_init();
    std::size_t max_bytes;
    explicit Transfer(HttpRequest job, std::size_t limit) : request(std::move(job)), max_bytes(limit) {}
    ~Transfer() { if (easy) curl_easy_cleanup(easy); }
};

Http::Http(const Options& options)
    : max_bytes_(options.max_response_bytes), max_connections_(options.max_connections),
      timeout_ms_(static_cast<long>(options.request_timeout.count())) {}

std::expected<std::unique_ptr<Http>, Error> Http::create(const Options& options) {
    static CurlRuntime runtime;
    if (runtime.result != CURLE_OK)
        return std::unexpected(Error{ErrorCode::Network, static_cast<int>(runtime.result), curl_easy_strerror(runtime.result)});
    const auto* version = curl_version_info(CURLVERSION_NOW);
    if (!(version->features & CURL_VERSION_ASYNCHDNS))
        return std::unexpected(Error{ErrorCode::Network, 0, "libcurl needs asynchronous DNS for nonblocking lookups"});
    auto http = std::unique_ptr<Http>(new Http(options));
    http->multi_ = curl_multi_init();
    if (!http->multi_) return std::unexpected(Error{ErrorCode::Network, 0, "curl_multi_init failed"});
    auto configure = [&](CURLMoption option, auto value) {
        return curl_multi_setopt(http->multi_, option, value);
    };
    for (auto code : {
        configure(CURLMOPT_SOCKETFUNCTION, &Http::socket_callback),
        configure(CURLMOPT_SOCKETDATA, http.get()),
        configure(CURLMOPT_TIMERFUNCTION, &Http::timer_callback),
        configure(CURLMOPT_TIMERDATA, http.get()),
        configure(CURLMOPT_MAX_TOTAL_CONNECTIONS, static_cast<long>(options.max_connections)),
        configure(CURLMOPT_MAXCONNECTS, static_cast<long>(options.max_connections))
    }) if (code != CURLM_OK) return std::unexpected(multi_error(code));
    http->active_.reserve(options.max_connections);
    http->sockets_.reserve(options.max_connections * 2);
    return http;
}

Http::~Http() {
    cancel();
    if (multi_) curl_multi_cleanup(multi_);
}

int Http::socket_callback(CURL*, curl_socket_t socket, int action, void* user, void*) noexcept {
    auto& self = *static_cast<Http*>(user);
    const auto at = std::find_if(self.sockets_.begin(), self.sockets_.end(),
        [&](const auto& fd) { return fd.fd == socket; });
    if (action == CURL_POLL_REMOVE) {
        if (at != self.sockets_.end()) self.sockets_.erase(at);
        return 0;
    }
    short events = 0;
    if (action == CURL_POLL_IN || action == CURL_POLL_INOUT) events |= POLLIN;
    if (action == CURL_POLL_OUT || action == CURL_POLL_INOUT) events |= POLLOUT;
    if (at != self.sockets_.end()) at->events = events;
    else {
        try { self.sockets_.push_back({socket, events, 0}); }
        catch (...) { self.callback_failed_ = true; return -1; }
    }
    return 0;
}

int Http::timer_callback(CURLM*, long timeout_ms, void* user) noexcept {
    auto& self = *static_cast<Http*>(user);
    if (timeout_ms < 0) self.deadline_.reset();
    else self.deadline_ = Clock::now() + Milliseconds{timeout_ms};
    return 0;
}

std::size_t Http::write_callback(char* data, std::size_t size, std::size_t count, void* user) noexcept {
    auto& transfer = *static_cast<Transfer*>(user);
    if (size && count > std::numeric_limits<std::size_t>::max() / size) return 0;
    const auto bytes = size * count;
    if (bytes > transfer.max_bytes - transfer.body.size()) return 0;
    try { transfer.body.append(data, bytes); }
    catch (...) { return 0; }
    return bytes;
}

bool Http::enqueue(HttpRequest request) {
    // A provider's candidate fanout is bounded independently of body limits.
    if (queue_.size() + active_.size() >= 16) return false;
    queue_.push_back(std::move(request));
    pump();
    return true;
}

void Http::pump() {
    while (active_.size() < max_connections_ && !queue_.empty()) {
        auto transfer = std::make_unique<Transfer>(std::move(queue_.front()), max_bytes_);
        queue_.pop_front();
        if (!transfer->easy) {
            failures_.push_back({transfer->request.id, {}, CURLE_OUT_OF_MEMORY, 0});
            continue;
        }
        CURLcode configuration = CURLE_OK;
        auto set = [&](CURLoption option, auto value) {
            const auto code = curl_easy_setopt(transfer->easy, option, value);
            if (code != CURLE_OK) configuration = code;
        };
        set(CURLOPT_URL, transfer->request.url.c_str());
        set(CURLOPT_USERAGENT, "TideIsland-lyricsmpris/3.0");
        set(CURLOPT_WRITEFUNCTION, &Http::write_callback);
        set(CURLOPT_WRITEDATA, transfer.get());
        set(CURLOPT_PRIVATE, transfer.get());
        set(CURLOPT_TIMEOUT_MS, timeout_ms_);
        set(CURLOPT_CONNECTTIMEOUT_MS, std::min(timeout_ms_, 3000L));
        set(CURLOPT_NOSIGNAL, 1L);
        set(CURLOPT_FOLLOWLOCATION, 1L);
        set(CURLOPT_MAXREDIRS, 3L);
        set(CURLOPT_PROTOCOLS_STR, "http,https");
        set(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
        set(CURLOPT_ACCEPT_ENCODING, "");
        if (!transfer->request.referer.empty()) set(CURLOPT_REFERER, transfer->request.referer.c_str());
        // The cookie engine is deliberately never enabled, including NetEase.
        if (configuration != CURLE_OK) {
            failures_.push_back({transfer->request.id, {}, configuration, 0});
            continue;
        }
        const auto code = curl_multi_add_handle(multi_, transfer->easy);
        if (code != CURLM_OK) {
            failures_.push_back({transfer->request.id, {}, CURLE_FAILED_INIT, 0});
            continue;
        }
        active_.push_back(std::move(transfer));
    }
    if (!failures_.empty()) deadline_ = Clock::now();
}

std::expected<std::vector<HttpResponse>, Error> Http::process(std::span<const pollfd> ready, Clock::time_point now) {
    int running = 0;
    if (deadline_ && now >= *deadline_) {
        deadline_.reset();
        const auto code = curl_multi_socket_action(multi_, CURL_SOCKET_TIMEOUT, 0, &running);
        if (code != CURLM_OK) return std::unexpected(multi_error(code));
    }
    for (const auto& fd : ready) {
        if (!fd.revents || std::none_of(sockets_.begin(), sockets_.end(),
            [&](const auto& interest) { return interest.fd == fd.fd; })) continue;
        int events = 0;
        if (fd.revents & POLLIN) events |= CURL_CSELECT_IN;
        if (fd.revents & POLLOUT) events |= CURL_CSELECT_OUT;
        if (fd.revents & (POLLERR | POLLHUP | POLLNVAL)) events |= CURL_CSELECT_ERR;
        const auto code = curl_multi_socket_action(multi_, fd.fd, events, &running);
        if (code != CURLM_OK) return std::unexpected(multi_error(code));
    }
    if (callback_failed_) return std::unexpected(Error{ErrorCode::Network, 0, "HTTP socket registration failed"});
    std::vector<HttpResponse> responses = std::move(failures_);
    failures_.clear();
    int remaining = 0;
    while (auto* message = curl_multi_info_read(multi_, &remaining)) {
        if (message->msg != CURLMSG_DONE) continue;
        const auto at = std::find_if(active_.begin(), active_.end(),
            [&](const auto& transfer) { return transfer->easy == message->easy_handle; });
        if (at == active_.end()) continue;
        long status = 0;
        curl_easy_getinfo(message->easy_handle, CURLINFO_RESPONSE_CODE, &status);
        responses.push_back({(*at)->request.id, std::move((*at)->body), message->data.result, status});
        curl_multi_remove_handle(multi_, message->easy_handle);
        active_.erase(at);
    }
    pump();
    return responses;
}

void Http::cancel() noexcept {
    queue_.clear();
    if (multi_) for (auto& transfer : active_) curl_multi_remove_handle(multi_, transfer->easy);
    active_.clear();
    if (multi_) {
        int remaining = 0;
        while (curl_multi_info_read(multi_, &remaining)) {}
    }
    failures_.clear();
    sockets_.clear();
    deadline_.reset();
    callback_failed_ = false;
}

} // namespace lyricsmpris::detail
