#pragma once

#include "../Client.hpp"

#include <curl/curl.h>
#include <deque>

namespace lyricsmpris::detail {

struct HttpRequest {
    std::uint64_t id;
    std::string url;
    std::string referer;
};
struct HttpResponse {
    std::uint64_t id;
    std::string body;
    CURLcode error;
    long status;
};

class Http {
public:
    static std::expected<std::unique_ptr<Http>, Error> create(const Options& options);
    ~Http();
    bool enqueue(HttpRequest request);
    void cancel() noexcept;
    std::expected<std::vector<HttpResponse>, Error> process(std::span<const pollfd> ready, Clock::time_point now);
    std::span<const pollfd> poll_fds() const noexcept { return sockets_; }
    std::optional<Clock::time_point> deadline() const noexcept { return deadline_; }
    bool busy() const noexcept { return !active_.empty() || !queue_.empty(); }

private:
    struct Transfer;
    explicit Http(const Options& options);
    void pump();
    static int socket_callback(CURL*, curl_socket_t, int, void*, void*) noexcept;
    static int timer_callback(CURLM*, long, void*) noexcept;
    static std::size_t write_callback(char*, std::size_t, std::size_t, void*) noexcept;

    CURLM* multi_ = nullptr;
    std::vector<std::unique_ptr<Transfer>> active_;
    std::deque<HttpRequest> queue_;
    std::vector<pollfd> sockets_;
    std::vector<HttpResponse> failures_;
    std::optional<Clock::time_point> deadline_;
    std::size_t max_bytes_;
    unsigned max_connections_;
    long timeout_ms_;
    bool callback_failed_ = false;
};

} // namespace lyricsmpris::detail
