#include "app/main_loop.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <poll.h>
#include <unistd.h>

#include "backend/sysinfo/sysinfo.hpp"
#include "backend/timer.hpp"
#include "backend/wayland.hpp"
#include "command/command.hpp"
#include "music/media.hpp"
#include "music/spotify_bridge.hpp"
#include "utils/log.hpp"

using namespace std;
using namespace std::chrono;

namespace {

int            wayland_fd{-1};
bool           is_stdin_open{true};
vector<pollfd> fds;

}

namespace MainLoop {

bool wait(bool is_animating, nanoseconds frame_delay) {
	if(wayland_fd == -1) {
		wayland_fd = Wayland::get_wayland_fd();
		if(wayland_fd == -1)
			Log::fatal("Failed to get Wayland file descriptor");
	}

	const short wayland_events{Wayland::prepare_events()};

	fds.clear();
	fds.push_back({.fd = wayland_fd, .events = wayland_events, .revents = 0});
	fds.push_back({.fd = Timer::get_timer_fd(), .events = POLLIN, .revents = 0});
	// A negative fd is ignored by poll and keeps the slot layout fixed.
	fds.push_back({.fd = is_stdin_open ? STDIN_FILENO : -1, .events = POLLIN, .revents = 0});
	fds.push_back({.fd = SysInfo::fd(), .events = POLLIN, .revents = 0});
	Media::append_poll_fds(fds);
	const size_t media_end{fds.size()};
	SpotifyBridge::append_poll_fds(fds);

	optional<nanoseconds> timeout{};
	if(is_animating)
		timeout = frame_delay > 0ns ? frame_delay : nanoseconds{16ms};
	const auto media_deadline{Media::next_deadline()};
	if(media_deadline) {
		const nanoseconds left{max(nanoseconds{*media_deadline - steady_clock::now()}, 0ns)};
		timeout = timeout ? min(*timeout, left) : left;
	}

	timespec  ts{};
	timespec const* ts_ptr{};
	if(timeout) {
		const auto secs{duration_cast<seconds>(*timeout)};
		ts     = {.tv_sec = secs.count(), .tv_nsec = (*timeout - secs).count()};
		ts_ptr = &ts;
	}

	int result{};
	do {
		result = ppoll(fds.data(), fds.size(), ts_ptr, nullptr);
	} while(result == -1 && errno == EINTR);

	if(result == -1) {
		Wayland::cancel_events();
		Log::fatal("poll failed: {}", strerror(errno));
	}

	Wayland::handle_events(fds[0].revents);

	if(fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
		Log::fatal("Timer fd poll error: {}", fds[1].revents);
	}

	{
		constexpr size_t         k_fixed = 4;
		const span<const pollfd> media_fds{fds.begin() + k_fixed, fds.begin() + static_cast<ptrdiff_t>(media_end)};
		if(const bool is_ready{ranges::any_of(media_fds, [](const pollfd& p) { return p.revents != 0; })};
			is_ready || (media_deadline && steady_clock::now() >= *media_deadline))
			Media::process(media_fds);
	}

	{
		if(const span<const pollfd> bridge_fds{fds.begin() + static_cast<ptrdiff_t>(media_end), fds.end()};
			ranges::any_of(bridge_fds, [](const pollfd& p) { return p.revents != 0; }))
			SpotifyBridge::process(bridge_fds);
	}

	if(fds[3].revents & POLLIN)
		SysInfo::process();

	if(fds[1].revents & POLLIN) {
		Timer::handle_events();
		return true;
	}

	if(fds[2].revents & (POLLIN | POLLHUP | POLLERR)) {
		string cmd{};
		if(getline(cin, cmd))
			Command::handle_command(cmd);
		else
			is_stdin_open = false;
	}

	const bool should_redraw{Wayland::get_wayland_redraw()};
	// A timeout only means "next animation frame" or a media deadline; the
	// latter does not need a frame by itself.
	return should_redraw || is_animating;
}

}
