#include <array>
#include <charconv>
#include <filesystem>
#include <string_view>

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "backend/sysinfo/source.hpp"
#include "utils/log.hpp"

using namespace std;

namespace Fs = filesystem;

namespace {

int             value_fd{-1};
long            max_value{};
optional<float> current{};

long read_long(int fd) {
	array<char, 32> buf{};
	const ssize_t   n{pread(fd, buf.data(), buf.size(), 0)};
	long            v{-1};
	if(n > 0)
		from_chars(buf.data(), buf.data() + n, v);
	return v;
}

long read_long(const Fs::path& path) {
	const int fd{open(path.c_str(), O_RDONLY | O_CLOEXEC)};
	if(fd == -1)
		return -1;
	const long v{read_long(fd)};
	close(fd);
	return v;
}

int type_rank(const Fs::path& dir) {
	array<char, 16> buf{};
	const int       fd{open((dir / "type").c_str(), O_RDONLY | O_CLOEXEC)};
	if(fd == -1)
		return 3;
	const ssize_t n{read(fd, buf.data(), buf.size())};
	close(fd);
	const string_view t{buf.data(), n > 0 ? static_cast<size_t>(n) : 0};
	if(t.starts_with("firmware"))
		return 0;
	if(t.starts_with("platform"))
		return 1;
	return 2;
}

bool refresh() {
	const long v{read_long(value_fd)};
	if(v < 0)
		return false;
	const float next{static_cast<float>(v) / static_cast<float>(max_value)};
	if(current == next)
		return false;
	current = next;
	return true;
}

}

namespace SysInfo::Backlight {

void init() {
	error_code ec{};
	Fs::path   best{};
	int        best_rank{4};
	for(const auto& e : Fs::directory_iterator("/sys/class/backlight", ec)) {
		if(const int r{type_rank(e.path())}; r < best_rank) {
			best_rank = r;
			best      = e.path();
		}
	}
	if(best.empty())
		return;

	max_value = read_long(best / "max_brightness");
	value_fd  = open((best / "actual_brightness").c_str(), O_RDONLY | O_CLOEXEC);
	if(max_value <= 0 || value_fd == -1) {
		shutdown();
		return;
	}
	// The kernel sysfs_notify()s actual_brightness on every change; it shows up as EPOLLPRI.
	refresh();
	if(!Detail::watch(value_fd, Detail::Tag::Backlight, EPOLLPRI))
		shutdown();
	else
		Log::logger(Log::LogLevel::Debug, R"_(SysInfo: backlight path="{}")_", best.string());
}

void shutdown() {
	if(value_fd != -1) {
		Detail::unwatch(value_fd);
		close(value_fd);
	}
	value_fd = -1;
	current.reset();
}

bool dispatch() {
	return value_fd != -1 && refresh();
}

optional<float> get_brightness() {
	return current;
}

}
