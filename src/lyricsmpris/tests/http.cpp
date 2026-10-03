#include "Check.hpp"
#include "lyricsmpris/detail/Http.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace lyricsmpris;
using namespace lyricsmpris::detail;
using namespace std::chrono_literals;

struct Server {
    pid_t child = -1;
    unsigned port = 0;
    Server() {
        const auto listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        CHECK(listener >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        CHECK(listen(listener, 8) == 0);
        socklen_t size = sizeof(address);
        CHECK(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0);
        port = ntohs(address.sin_port);
        child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            while (true) {
                const auto client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
                if (client < 0) _exit(0);
                std::string request;
                char buffer[4096];
                while (request.find("\r\n\r\n") == request.npos && request.size() < 16384) {
                    const auto count = recv(client, buffer, sizeof(buffer), 0);
                    if (count <= 0) break;
                    request.append(buffer, static_cast<std::size_t>(count));
                }
                std::string body = "small";
                std::string headers;
                if (request.starts_with("GET /large ")) body = std::string(512, 'x');
                if (request.starts_with("GET /cookie-set ")) headers = "Set-Cookie: NMTID=bad; Path=/\r\n";
                if (request.starts_with("GET /cookie-check ")) body = request.find("Cookie:") == request.npos ? "no-cookie" : "cookie-reused";
                if (request.starts_with("GET /slow ")) poll(nullptr, 0, 250);
                const auto response = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size())
                    + "\r\nConnection: close\r\n" + headers + "\r\n" + body;
                std::size_t sent = 0;
                while (sent < response.size()) {
                    const auto count = send(client, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
                    if (count <= 0) break;
                    sent += static_cast<std::size_t>(count);
                }
                close(client);
            }
        }
        close(listener);
    }
    ~Server() {
        if (child > 0) { kill(child, SIGTERM); waitpid(child, nullptr, 0); }
    }
    std::string url(std::string_view path) const { return "http://127.0.0.1:" + std::to_string(port) + std::string(path); }
};

std::vector<HttpResponse> finish(Http& http, std::size_t count) {
    std::vector<HttpResponse> result;
    std::vector<pollfd> ready;
    const auto end = Clock::now() + 3s;
    while (result.size() < count && Clock::now() < end) {
        auto responses = http.process(ready, Clock::now());
        CHECK(responses);
        for (auto& response : *responses) result.push_back(std::move(response));
        if (result.size() >= count) break;
        const auto interests = http.poll_fds();
        ready.assign(interests.begin(), interests.end());
        const auto deadline = std::min(end, http.deadline().value_or(end));
        const auto delay = std::chrono::ceil<Milliseconds>(deadline - Clock::now()).count();
        CHECK(poll(ready.data(), ready.size(), static_cast<int>(std::max<std::int64_t>(0, delay))) >= 0);
    }
    CHECK(result.size() == count);
    return result;
}

int main() {
    try {
        Server server;
        Options options; options.max_response_bytes = 64; options.request_timeout = 1s;
        auto http = Http::create(options);
        CHECK(http);
        CHECK((*http)->enqueue({1, server.url("/small"), {}}));
        CHECK((*http)->enqueue({2, server.url("/large"), {}}));
        auto responses = finish(**http, 2);
        for (const auto& response : responses) {
            CHECK(response.body.size() <= 64);
            if (response.id == 1) CHECK(response.error == CURLE_OK && response.body == "small");
            else CHECK(response.id == 2 && response.error == CURLE_WRITE_ERROR);
        }
        CHECK((*http)->enqueue({3, server.url("/cookie-set"), {}}));
        CHECK(finish(**http, 1).front().error == CURLE_OK);
        CHECK((*http)->enqueue({4, server.url("/cookie-check"), {}}));
        CHECK(finish(**http, 1).front().body == "no-cookie");
        CHECK((*http)->enqueue({5, server.url("/slow"), {}}));
        (*http)->cancel();
        CHECK(!(*http)->busy() && !(*http)->deadline());
        CHECK((*http)->enqueue({6, server.url("/small"), {}}));
        auto after_cancel = finish(**http, 1);
        CHECK(after_cancel.front().id == 6 && after_cancel.front().body == "small");
        for (std::uint64_t id = 10; id < 26; ++id) CHECK((*http)->enqueue({id, server.url("/small"), {}}));
        CHECK(!(*http)->enqueue({26, server.url("/small"), {}}));
        (*http)->cancel();
        options.request_timeout = 40ms;
        auto timeout_http = Http::create(options);
        CHECK(timeout_http);
        CHECK((*timeout_http)->enqueue({30, server.url("/slow"), {}}));
        CHECK(finish(**timeout_http, 1).front().error == CURLE_OPERATION_TIMEDOUT);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "HTTP resource and cancellation checks passed\n";
}
