#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <string>
#include <string_view>

#include <dirent.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "backend/sysinfo/source.hpp"
#include "utils/log.hpp"

using namespace std;

namespace {

constexpr string_view k_root{"/sys/class/power_supply/"};

int                      uevent_fd{-1};
optional<SysInfo::Power> current{};

string_view read_attr(const string& dir, const char* attr, array<char, 32>& buf) {
	const int fd{open((dir + attr).c_str(), O_RDONLY | O_CLOEXEC)};
	if(fd == -1)
		return {};
	const ssize_t n{read(fd, buf.data(), buf.size())};
	close(fd);
	string_view v{buf.data(), n > 0 ? static_cast<size_t>(n) : 0};
	while(!v.empty() && (v.back() == '\n' || v.back() == ' '))
		v.remove_suffix(1);
	return v;
}

SysInfo::Charge parse_status(string_view s) {
	using enum SysInfo::Charge;
	if(s == "Charging")
		return Charging;
	if(s == "Discharging")
		return Discharging;
	if(s == "Not charging")
		return NotCharging;
	if(s == "Full")
		return Full;
	return Unknown;
}

optional<SysInfo::Power> scan() {
	DIR* d{opendir(k_root.data())};

	if(!d)
		return nullopt;

	optional<SysInfo::Power> out{};
	int                      batteries{};
	int                      capacity_sum{};
	bool                     has_any_charging{}, is_all_full{true};
	array<char, 32>          buf{};

	while(const dirent* e{readdir(d)}) {
		if(e->d_name[0] == '.')
			continue;
		const string dir{string{k_root} + e->d_name + "/"};
		const string type{read_attr(dir, "type", buf)};
		if(!out)
			out.emplace();

		if(type == "Mains" || type == "USB") {
			out->is_plugged_ |= read_attr(dir, "online", buf) == "1";
			continue;
		}
		// scope=Device: mice, headsets, ... not the machine's own battery.
		if(type != "Battery" || read_attr(dir, "scope", buf) == "Device")
			continue;

		int        cap{};
		const auto v{read_attr(dir, "capacity", buf)};
		if(from_chars(v.data(), v.data() + v.size(), cap).ec != errc{})
			continue;
		const auto status{parse_status(read_attr(dir, "status", buf))};
		++batteries;
		capacity_sum += cap;
		has_any_charging |= status == SysInfo::Charge::Charging;
		is_all_full &= status == SysInfo::Charge::Full;
		if(batteries == 1)
			out->status_ = status;
	}
	closedir(d);

	if(out && batteries) {
		out->battery_ = capacity_sum / batteries;
		if(has_any_charging)
			out->status_ = SysInfo::Charge::Charging;
		else if(is_all_full)
			out->status_ = SysInfo::Charge::Full;
	}
	return out;
}

bool refresh() {
	auto next{scan()};
	if(current == next)
		return false;
	current = next;
	return true;
}

bool is_power_supply(string_view msg) {
	// "action@devpath\0KEY=value\0KEY=value\0..."
	for(size_t pos{0}; pos < msg.size();) {
		const size_t end{msg.find('\0', pos)};
		if(msg.substr(pos, end - pos) == "SUBSYSTEM=power_supply")
			return true;
		if(end == string_view::npos)
			break;
		pos = end + 1;
	}
	return false;
}

}

namespace SysInfo::Supply {

void init() {
	refresh();
	uevent_fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	if(uevent_fd == -1)
		return;
	// Group 1 = raw kernel uevents; no udevd involved, readable without privileges.
	const sockaddr_nl addr{.nl_family = AF_NETLINK, .nl_pad = 0, .nl_pid = 0, .nl_groups = 1};
	if(bind(uevent_fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) == -1 ||
		!Detail::watch(uevent_fd, Detail::Tag::Power, EPOLLIN)) {
		Log::logger(Log::LogLevel::Warning, "SysInfo: uevent socket failed: {}", strerror(errno));
		shutdown();
	}
}

void shutdown() {
	if(uevent_fd != -1) {
		Detail::unwatch(uevent_fd);
		close(uevent_fd);
	}
	uevent_fd = -1;
	current.reset();
}

bool dispatch() {
	array<char, 8192> buf{};
	bool              has_relevant{};
	for(;;) {
		sockaddr_nl src{};
		iovec       iov{.iov_base = buf.data(), .iov_len = buf.size()};
		msghdr      msg{};
		msg.msg_name    = &src;
		msg.msg_namelen = sizeof src;
		msg.msg_iov     = &iov;
		msg.msg_iovlen  = 1;

		const ssize_t n{recvmsg(uevent_fd, &msg, 0)};
		if(n == -1 && errno == EINTR)
			continue;
		if(n == -1 && errno == ENOBUFS) {
			has_relevant = true;
			continue;
		}
		if(n <= 0)
			break;
		if(src.nl_pid != 0)
			continue;
		has_relevant |= is_power_supply({buf.data(), static_cast<size_t>(n)});
	}
	return has_relevant && refresh();
}

optional<SysInfo::Power> get_power() {
	return current;
}

}
