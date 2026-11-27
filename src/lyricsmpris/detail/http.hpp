#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <curl/curl.h>

#include "../client.hpp"

namespace LyricsMpris::Detail {

/// @brief Describes an HTTP request queued by a provider.
struct HttpRequest {
	uint64_t    id_{};
	std::string url_{};
	std::string referer_{};
};

/// @brief Describes a completed HTTP response.
struct HttpResponse {
	uint64_t    id_{};
	std::string body_{};
	CURLcode    error_{};
	long        status_{};
};

/// @brief Nonblocking multi-handle HTTP client for lyrics providers.
class Http {
public:
	/// @brief Creates an HTTP client with the given resource limits.
	static std::expected<std::unique_ptr<Http>, Error> create(const Options& options);
	/// @brief Cancels pending transfers and releases the multi handle.
	~Http();
	/// @brief Queues a request and starts it when capacity allows.
	bool enqueue(HttpRequest request);
	/// @brief Cancels all pending and active transfers.
	void cancel();
	/// @brief Drives transfers and returns completed responses.
	std::expected<std::vector<HttpResponse>, Error> process(std::span<const pollfd> ready, Clock::time_point now);

	/// @brief Returns the descriptors the client is interested in.
	std::span<const pollfd> get_poll_fds() const { return sockets_; }

	/// @brief Returns the next libcurl timer deadline, if any.
	std::optional<Clock::time_point> get_deadline() const { return deadline_; }

	/// @brief Returns true when transfers remain queued or active.
	bool is_busy() const { return !active_.empty() || !queue_.empty(); }

private:
	struct Transfer;
	explicit Http(const Options& options);
	void          pump();
	static int    socket_callback(CURL*, curl_socket_t, int, void*, void*) noexcept;
	static int    timer_callback(CURLM*, long, void*) noexcept;
	static size_t write_callback(char*, size_t, size_t, void*) noexcept;

	CURLM*                                 multi_{};
	std::vector<std::unique_ptr<Transfer>> active_{};
	std::deque<HttpRequest>                queue_{};
	std::vector<pollfd>                    sockets_{};
	std::vector<HttpResponse>              failures_{};
	std::optional<Clock::time_point>       deadline_{};
	size_t                                 max_bytes_{};
	unsigned                               max_connections_{};
	long                                   timeout_ms_{};
	bool                                   has_callback_failed_{};
};

}
