#include "backend/sysinfo/sysinfo.hpp"

#include <array>
#include <cerrno>
#include <cstring>

#include <sys/epoll.h>
#include <unistd.h>

#include "backend/sysinfo/source.hpp"
#include "utils/log.hpp"

using namespace std;

namespace {

int epoll_fd{-1};

void (*listener)(SysInfo::Update){};

uint64_t pack(int fd, SysInfo::Detail::Tag tag) {
	return static_cast<uint64_t>(to_underlying(tag)) << 32 | static_cast<uint32_t>(fd);
}

}

namespace SysInfo {

namespace Detail {

bool watch(int fd, Tag tag, uint32_t events) {
	epoll_event ev{.events = events, .data = {.u64 = pack(fd, tag)}};
	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == 0)
		return true;
	Log::logger(Log::LogLevel::Warning, "SysInfo: epoll_ctl add failed: {}", strerror(errno));
	return false;
}

void unwatch(int fd) {
	if(epoll_fd != -1)
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
}

}

void init() {
	if(epoll_fd != -1)
		return;
	epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if(epoll_fd == -1) {
		Log::logger(Log::LogLevel::Error, "SysInfo: epoll_create1 failed: {}", strerror(errno));
		return;
	}
	Pipewire::init();
	Backlight::init();
	Supply::init();
}

void shutdown() {
	Supply::shutdown();
	Backlight::shutdown();
	Pipewire::shutdown();
	if(epoll_fd != -1)
		close(epoll_fd);
	epoll_fd = -1;
}

int fd() {
	return epoll_fd;
}

void process() {
	if(epoll_fd == -1)
		return;
	array<epoll_event, 16> evs{};
	const int              n{epoll_wait(epoll_fd, evs.data(), evs.size(), 0)};
	Update                 changed{};
	for(int i{0}; i < n; ++i) {
		switch(static_cast<Detail::Tag>(evs[i].data.u64 >> 32)) {
			case Detail::Tag::Pipewire:
				if(Pipewire::dispatch())
					changed = changed | Update::Volume;
				break;
			case Detail::Tag::Backlight:
				if(Backlight::dispatch())
					changed = changed | Update::Brightness;
				break;
			case Detail::Tag::Power:
				if(Supply::dispatch())
					changed = changed | Update::Power;
				break;
		}
	}
	if(changed != Update::None && listener)
		listener(changed);
}

void set_listener(void (*l)(Update)) {
	listener = l;
}

optional<Volume> get_volume() {
	return Pipewire::get_volume();
}

optional<float> get_brightness() {
	return Backlight::get_brightness();
}

optional<Power> get_power() {
	return Supply::get_power();
}

}
