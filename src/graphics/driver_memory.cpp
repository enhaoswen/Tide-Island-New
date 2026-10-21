#include "graphics/driver_memory.hpp"

#if defined(__linux__)
#include "utils/log.hpp"

#include <GLES3/gl3.h>
#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <sys/mman.h>

namespace {

struct Mapping {
    std::uintptr_t begin{}, end{};
    std::array<char, 5> permissions{};
    bool compiler{};
    std::optional<std::size_t> anonymous, private_dirty, shared_dirty, swap, locked, rss;

    bool eligible() const {
        const std::string_view access{permissions.data()};
        // Even an r--p segment can contain relocated, copy-on-write data.
        // Require every accounting field, and leave all such data intact.
        return compiler && begin < end && (access == "r--p" || access == "r-xp") &&
            anonymous && *anonymous == 0 && private_dirty && *private_dirty == 0 &&
            shared_dirty && *shared_dirty == 0 && swap && *swap == 0 &&
            locked && *locked == 0 && rss;
    }
};

bool compiler_file(std::string_view path) {
    if (!path.starts_with('/')) {
        return false;
    }
    const auto name = path.substr(path.find_last_of('/') + 1);
    constexpr std::string_view prefix = "libnvidia-gpucomp.so.";
    if (!name.starts_with(prefix)) {
        return false;
    }
    const auto version = name.substr(prefix.size());
    // Reject replaced/deleted libraries and unrecognized names.
    return !version.empty() && version.front() >= '0' && version.front() <= '9' &&
        version.find_first_not_of("0123456789.") == std::string_view::npos;
}

} // namespace
#endif

void DriverMemory::release_idle_compiler_pages() {
#if defined(__linux__)
    const char* setting = std::getenv("TIDE_NVIDIA_RECLAIM_COMPILER");
    if (setting && std::string_view(setting) == "0") {
        Log::logger(Log::Debug, "NVIDIA compiler page reclamation disabled");
        return;
    }
    const auto* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    if (!vendor || std::string_view(vendor) != "NVIDIA Corporation") {
        return;
    }

    // First use can trigger driver compilation beyond program linking.
    // Finish that work before offering clean compiler file pages for reclaim.
    glFinish();
    glReleaseShaderCompiler();

    std::ifstream stream("/proc/self/smaps");
    if (!stream) {
        Log::logger(Log::Debug, "Skipping NVIDIA compiler page reclamation: smaps unavailable");
        return;
    }
    std::vector<Mapping> selected;
    Mapping current;
    std::string line;
    while (std::getline(stream, line)) {
        std::uintptr_t begin{}, end{};
        std::array<char, 5> permissions{};
        int path_offset{};
        if (std::sscanf(line.c_str(), "%" SCNxPTR "-%" SCNxPTR " %4s %*s %*s %*s %n",
                &begin, &end, permissions.data(), &path_offset) == 3 && path_offset > 0) {
            if (current.eligible()) {
                selected.push_back(current);
            }
            current = {};
            current.begin = begin;
            current.end = end;
            current.permissions = permissions;
            current.compiler = compiler_file(std::string_view(line).substr(path_offset));
        }
        else {
            std::size_t value{};
            if (std::sscanf(line.c_str(), "Anonymous: %zu kB", &value) == 1)
                current.anonymous = value;
            else if (std::sscanf(line.c_str(), "Private_Dirty: %zu kB", &value) == 1)
                current.private_dirty = value;
            else if (std::sscanf(line.c_str(), "Shared_Dirty: %zu kB", &value) == 1)
                current.shared_dirty = value;
            else if (std::sscanf(line.c_str(), "Swap: %zu kB", &value) == 1)
                current.swap = value;
            else if (std::sscanf(line.c_str(), "Locked: %zu kB", &value) == 1)
                current.locked = value;
            else if (std::sscanf(line.c_str(), "Rss: %zu kB", &value) == 1)
                current.rss = value;
        }
    }
    if (!stream.eof()) {
        Log::logger(Log::Debug, "Skipping NVIDIA compiler page reclamation: incomplete smaps");
        return;
    }
    if (current.eligible()) {
        selected.push_back(current);
    }
    stream.close();

    std::size_t advised_ranges{}, previous_rss_kib{};
    for (const auto& mapping : selected) {
        auto* address = reinterpret_cast<void*>(mapping.begin);
        const auto size = mapping.end - mapping.begin;
        // Keep file huge pages from immediately repopulating large ranges.
        // Preserve the virtual mapping; future compiler accesses fault back in.
        if (madvise(address, size, MADV_NOHUGEPAGE) != 0 ||
            madvise(address, size, MADV_DONTNEED) != 0) {
            const int error = errno;
            Log::logger(Log::Debug, "Skipping NVIDIA compiler page range: errno={}", error);
            continue;
        }
        ++advised_ranges;
        previous_rss_kib += *mapping.rss;
    }
    Log::logger(Log::Debug, "NVIDIA compiler file pages: advised_ranges={}, previous_rss_kib={}",
        advised_ranges, previous_rss_kib);
#endif
}
