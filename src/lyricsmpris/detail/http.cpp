#include "http.hpp"

#include <algorithm>
#include <limits>
#include <utility>

using namespace std;

namespace LyricsMpris::Detail {
namespace {

struct CurlRuntime {
	CURLcode result_{curl_global_init(CURL_GLOBAL_DEFAULT)};

	~CurlRuntime() {
		if(result_ == CURLE_OK)
			curl_global_cleanup();
	}
};

const CurlRuntime k_runtime{};

Error multi_error(CURLMcode code) {
	return {ErrorCode::Network, static_cast<int>(code), curl_multi_strerror(code)};
}

}

struct Http::Transfer {
	HttpRequest request_{};
	string      body_{};
	CURL*       easy_{curl_easy_init()};
	size_t      max_bytes_{};

	explicit Transfer(HttpRequest job, size_t limit) :
		request_(std::move(job)),
		max_bytes_(limit) {}

	~Transfer() {
		if(easy_)
			curl_easy_cleanup(easy_);
	}
};

Http::Http(const Options& options) :
	max_bytes_(options.max_response_bytes_),
	max_connections_(options.max_connections_),
	timeout_ms_(static_cast<long>(options.request_timeout_.count())) {
}

expected<unique_ptr<Http>, Error> Http::create(const Options& options) {
	if(k_runtime.result_ != CURLE_OK)
		return unexpected(
			Error{ErrorCode::Network, static_cast<int>(k_runtime.result_), curl_easy_strerror(k_runtime.result_)});
	const auto* version{curl_version_info(CURLVERSION_NOW)};
	if(!(version->features & CURL_VERSION_ASYNCHDNS))
		return unexpected(Error{ErrorCode::Network, 0, "libcurl needs asynchronous DNS for nonblocking lookups"});
	auto http{unique_ptr<Http>(new Http(options))};
	http->multi_ = curl_multi_init();
	if(!http->multi_)
		return unexpected(Error{ErrorCode::Network, 0, "curl_multi_init failed"});
	const auto configure = [&](CURLMoption option, auto value) {
		return curl_multi_setopt(http->multi_, option, value);
	};
	for(const auto code : {configure(CURLMOPT_SOCKETFUNCTION, &Http::socket_callback),
			configure(CURLMOPT_SOCKETDATA, http.get()),
			configure(CURLMOPT_TIMERFUNCTION, &Http::timer_callback),
			configure(CURLMOPT_TIMERDATA, http.get()),
			configure(CURLMOPT_MAX_TOTAL_CONNECTIONS, static_cast<long>(options.max_connections_)),
			configure(CURLMOPT_MAXCONNECTS, static_cast<long>(options.max_connections_))})
		if(code != CURLM_OK)
			return unexpected(multi_error(code));
	http->active_.reserve(options.max_connections_);
	http->sockets_.reserve(options.max_connections_ * 2);
	return http;
}

Http::~Http() {
	cancel();
	if(multi_)
		curl_multi_cleanup(multi_);
}

int Http::socket_callback(CURL*, curl_socket_t socket, int action, void* user, void*) noexcept {
	auto&      self{*static_cast<Http*>(user)};
	const auto at =
		find_if(self.sockets_.begin(), self.sockets_.end(), [&](const auto& fd) { return fd.fd == socket; });
	if(action == CURL_POLL_REMOVE) {
		if(at != self.sockets_.end())
			self.sockets_.erase(at);
		return 0;
	}
	short events{};
	if(action == CURL_POLL_IN || action == CURL_POLL_INOUT)
		events |= POLLIN;
	if(action == CURL_POLL_OUT || action == CURL_POLL_INOUT)
		events |= POLLOUT;
	if(at != self.sockets_.end())
		at->events = events;
	else {
		try {
			self.sockets_.push_back({socket, events, 0});
		} catch(...) {
			self.has_callback_failed_ = true;
			return -1;
		}
	}
	return 0;
}

int Http::timer_callback(CURLM*, long timeout_ms, void* user) noexcept {
	auto& self{*static_cast<Http*>(user)};
	if(timeout_ms < 0)
		self.deadline_.reset();
	else
		self.deadline_ = Clock::now() + Milliseconds{timeout_ms};
	return 0;
}

size_t Http::write_callback(char* data, size_t size, size_t count, void* user) noexcept {
	auto& transfer{*static_cast<Transfer*>(user)};
	if(size && count > numeric_limits<size_t>::max() / size)
		return 0;
	const auto bytes{size * count};
	if(bytes > transfer.max_bytes_ - transfer.body_.size())
		return 0;
	try {
		transfer.body_.append(data, bytes);
	} catch(...) {
		return 0;
	}
	return bytes;
}

bool Http::enqueue(HttpRequest request) {
	// A provider's candidate fanout is bounded independently of body limits.
	if(queue_.size() + active_.size() >= 16)
		return false;
	queue_.push_back(std::move(request));
	pump();
	return true;
}

void Http::pump() {
	while(active_.size() < max_connections_ && !queue_.empty()) {
		auto transfer{make_unique<Transfer>(std::move(queue_.front()), max_bytes_)};
		queue_.pop_front();
		if(!transfer->easy_) {
			failures_.push_back({transfer->request_.id_, {}, CURLE_OUT_OF_MEMORY, 0});
			continue;
		}
		CURLcode   configuration{CURLE_OK};
		const auto set = [&](CURLoption option, auto value) {
			const auto code{curl_easy_setopt(transfer->easy_, option, value)};
			if(code != CURLE_OK)
				configuration = code;
		};
		set(CURLOPT_URL, transfer->request_.url_.c_str());
		set(CURLOPT_USERAGENT, "TideIsland-lyricsmpris/3.0");
		set(CURLOPT_WRITEFUNCTION, &Http::write_callback);
		set(CURLOPT_WRITEDATA, transfer.get());
		set(CURLOPT_PRIVATE, transfer.get());
		set(CURLOPT_TIMEOUT_MS, timeout_ms_);
		set(CURLOPT_CONNECTTIMEOUT_MS, min(timeout_ms_, 3000L));
		set(CURLOPT_NOSIGNAL, 1L);
		set(CURLOPT_FOLLOWLOCATION, 1L);
		set(CURLOPT_MAXREDIRS, 3L);
		set(CURLOPT_PROTOCOLS_STR, "http,https");
		set(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
		set(CURLOPT_ACCEPT_ENCODING, "");
		if(!transfer->request_.referer_.empty())
			set(CURLOPT_REFERER, transfer->request_.referer_.c_str());
		// The cookie engine is deliberately never enabled, including NetEase.
		if(configuration != CURLE_OK) {
			failures_.push_back({transfer->request_.id_, {}, configuration, 0});
			continue;
		}
		const auto code{curl_multi_add_handle(multi_, transfer->easy_)};
		if(code != CURLM_OK) {
			failures_.push_back({transfer->request_.id_, {}, CURLE_FAILED_INIT, 0});
			continue;
		}
		active_.push_back(std::move(transfer));
	}
	if(!failures_.empty())
		deadline_ = Clock::now();
}

expected<vector<HttpResponse>, Error> Http::process(span<const pollfd> ready, Clock::time_point now) {
	int running{};
	if(deadline_ && now >= *deadline_) {
		deadline_.reset();
		const auto code{curl_multi_socket_action(multi_, CURL_SOCKET_TIMEOUT, 0, &running)};
		if(code != CURLM_OK)
			return unexpected(multi_error(code));
	}
	for(const auto& fd : ready) {
		if(!fd.revents ||
			none_of(sockets_.begin(), sockets_.end(), [&](const auto& interest) { return interest.fd == fd.fd; }))
			continue;
		int events{};
		if(fd.revents & POLLIN)
			events |= CURL_CSELECT_IN;
		if(fd.revents & POLLOUT)
			events |= CURL_CSELECT_OUT;
		if(fd.revents & (POLLERR | POLLHUP | POLLNVAL))
			events |= CURL_CSELECT_ERR;
		const auto code{curl_multi_socket_action(multi_, fd.fd, events, &running)};
		if(code != CURLM_OK)
			return unexpected(multi_error(code));
	}
	if(has_callback_failed_)
		return unexpected(Error{ErrorCode::Network, 0, "HTTP socket registration failed"});
	vector<HttpResponse> responses{std::move(failures_)};
	failures_.clear();
	int remaining{};
	while(auto* message = curl_multi_info_read(multi_, &remaining)) {
		if(message->msg != CURLMSG_DONE)
			continue;
		const auto at = find_if(active_.begin(), active_.end(), [&](const auto& transfer) {
			return transfer->easy_ == message->easy_handle;
		});
		if(at == active_.end())
			continue;
		long status{};
		curl_easy_getinfo(message->easy_handle, CURLINFO_RESPONSE_CODE, &status);
		responses.push_back({(*at)->request_.id_, std::move((*at)->body_), message->data.result, status});
		curl_multi_remove_handle(multi_, message->easy_handle);
		active_.erase(at);
	}
	pump();
	return responses;
}

void Http::cancel() {
	queue_.clear();
	if(multi_)
		for(const auto& transfer : active_)
			curl_multi_remove_handle(multi_, transfer->easy_);
	active_.clear();
	if(multi_) {
		int remaining{};
		while(curl_multi_info_read(multi_, &remaining)) {
		}
	}
	failures_.clear();
	sockets_.clear();
	deadline_.reset();
	has_callback_failed_ = false;
}

}
