#include "backend/sysinfo/source.hpp"
#include "utils/log.hpp"

#include <array>
#include <charconv>
#include <fcntl.h>
#include <filesystem>
#include <string_view>
#include <sys/epoll.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

int                  value_fd{-1};
long                 max_value{};
std::optional<float> current{};

long read_long(int fd) {
    std::array<char, 32> buf;
    const ssize_t        n = pread(fd, buf.data(), buf.size(), 0);
    long                 v{-1};
    if (n > 0)
        std::from_chars(buf.data(), buf.data() + n, v);
    return v;
}

long read_long(const fs::path& path) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1)
        return -1;
    const long v = read_long(fd);
    close(fd);
    return v;
}

int type_rank(const fs::path& dir) {
    std::array<char, 16> buf;
    const int            fd = open((dir / "type").c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1)
        return 3;
    const ssize_t n = read(fd, buf.data(), buf.size());
    close(fd);
    const std::string_view t{buf.data(), n > 0 ? static_cast<size_t>(n) : 0};
    if (t.starts_with("firmware"))
        return 0;
    if (t.starts_with("platform"))
        return 1;
    return 2;
}

bool refresh() {
    const long v = read_long(value_fd);
    if (v < 0)
        return false;
    const float next = static_cast<float>(v) / static_cast<float>(max_value);
    if (current == next)
        return false;
    current = next;
    return true;
}

}

namespace SysInfo::Backlight {

void init() {
    std::error_code ec;
    fs::path        best;
    int             best_rank{4};
    for (const auto& e : fs::directory_iterator("/sys/class/backlight", ec)) {
        if (const int r = type_rank(e.path()); r < best_rank) {
            best_rank = r;
            best      = e.path();
        }
    }
    if (best.empty())
        return;

    max_value = read_long(best / "max_brightness");
    value_fd  = open((best / "actual_brightness").c_str(), O_RDONLY | O_CLOEXEC);
    if (max_value <= 0 || value_fd == -1) {
        shutdown();
        return;
    }
    // The kernel sysfs_notify()s actual_brightness on every change; it shows up as EPOLLPRI.
    refresh();
    if (!Detail::watch(value_fd, Detail::Tag::Backlight, EPOLLPRI))
        shutdown();
    else
        Log::logger(Log::Debug, R"_(SysInfo: backlight path="{}")_", best.string());
}

void shutdown() {
    if (value_fd != -1) {
        Detail::unwatch(value_fd);
        close(value_fd);
    }
    value_fd = -1;
    current.reset();
}

bool dispatch() { return value_fd != -1 && refresh(); }

std::optional<float> brightness() { return current; }

}
