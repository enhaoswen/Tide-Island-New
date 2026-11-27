#include "graphics/driver_memory.hpp"

#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <GLES3/gl3.h>
#include <sys/mman.h>

#include "utils/log.hpp"

using namespace std;

namespace {

struct Mapping {
	uintptr_t        begin_{}, end_{};
	array<char, 5>   permissions_{};
	bool             is_compiler_{};
	optional<size_t> anonymous_{}, private_dirty_{}, shared_dirty_{}, swap_{}, locked_{}, rss_{};

	[[nodiscard]] bool is_eligible() const {
		const string_view access{permissions_.data()};
		// Even an r--p segment can contain relocated, copy-on-write data.
		// Require every accounting field, and leave all such data intact.
		return is_compiler_ && begin_ < end_ && (access == "r--p" || access == "r-xp") && anonymous_ &&
			*anonymous_ == 0 && private_dirty_ && *private_dirty_ == 0 && shared_dirty_ && *shared_dirty_ == 0 &&
			swap_ && *swap_ == 0 && locked_ && *locked_ == 0 && rss_;
	}
};

bool is_compiler_file(string_view path) {
	if(!path.starts_with('/')) {
		return false;
	}
	const auto            name{path.substr(path.find_last_of('/') + 1)};
	constexpr string_view k_prefix{"libnvidia-gpucomp.so."};
	if(!name.starts_with(k_prefix)) {
		return false;
	}
	const auto version{name.substr(k_prefix.size())};
	// Reject replaced/deleted libraries and unrecognized names.
	return !version.empty() && version.front() >= '0' && version.front() <= '9' &&
		version.find_first_not_of("0123456789.") == string_view::npos;
}

}

namespace DriverMemory {
void release_idle_compiler_pages() {
#ifdef __linux__
	const char* setting{getenv("TIDE_NVIDIA_RECLAIM_COMPILER")};
	if(setting && string_view(setting) == "0") {
		Log::logger(Log::LogLevel::Debug, "NVIDIA compiler page reclamation disabled");
		return;
	}
	const auto* vendor{reinterpret_cast<const char*>(glGetString(GL_VENDOR))};
	if(!vendor || string_view(vendor) != "NVIDIA Corporation") {
		return;
	}

	// First use can trigger driver compilation beyond program linking.
	// Finish that work before offering clean compiler file pages for reclaim.
	glFinish();
	glReleaseShaderCompiler();

	ifstream stream("/proc/self/smaps");
	if(!stream) {
		Log::logger(Log::LogLevel::Debug, "Skipping NVIDIA compiler page reclamation: smaps unavailable");
		return;
	}
	vector<Mapping> selected{};
	Mapping         current{};
	string          line{};
	while(getline(stream, line)) {
		uintptr_t      begin{}, end{};
		array<char, 5> permissions{};
		int            path_offset{};
		if(sscanf(line.c_str(),
			   "%" SCNxPTR "-%" SCNxPTR " %4s %*s %*s %*s %n",
			   &begin,
			   &end,
			   permissions.data(),
			   &path_offset) == 3 &&
			path_offset > 0) {
			if(current.is_eligible()) {
				selected.push_back(current);
			}
			current              = {};
			current.begin_       = begin;
			current.end_         = end;
			current.permissions_ = permissions;
			current.is_compiler_ = is_compiler_file(string_view(line).substr(static_cast<size_t>(path_offset)));
		} else {
			size_t value{};
			if(sscanf(line.c_str(), "Anonymous: %zu kB", &value) == 1)
				current.anonymous_ = value;
			else if(sscanf(line.c_str(), "Private_Dirty: %zu kB", &value) == 1)
				current.private_dirty_ = value;
			else if(sscanf(line.c_str(), "Shared_Dirty: %zu kB", &value) == 1)
				current.shared_dirty_ = value;
			else if(sscanf(line.c_str(), "Swap: %zu kB", &value) == 1)
				current.swap_ = value;
			else if(sscanf(line.c_str(), "Locked: %zu kB", &value) == 1)
				current.locked_ = value;
			else if(sscanf(line.c_str(), "Rss: %zu kB", &value) == 1)
				current.rss_ = value;
		}
	}
	if(!stream.eof()) {
		Log::logger(Log::LogLevel::Debug, "Skipping NVIDIA compiler page reclamation: incomplete smaps");
		return;
	}
	if(current.is_eligible()) {
		selected.push_back(current);
	}
	stream.close();

	size_t advised_ranges{}, previous_rss_kib{};
	for(const auto& mapping : selected) {
		auto* const address = reinterpret_cast<void*>(mapping.begin_);
		// Keep file huge pages from immediately repopulating large ranges.
		// Preserve the virtual mapping; future compiler accesses fault back in.
		if(const auto size{mapping.end_ - mapping.begin_};
			madvise(address, size, MADV_NOHUGEPAGE) != 0 || madvise(address, size, MADV_DONTNEED) != 0) {
			const int error{errno};
			Log::logger(Log::LogLevel::Debug, "Skipping NVIDIA compiler page range: errno={}", error);
			continue;
		}
		++advised_ranges;
		previous_rss_kib += *mapping.rss_;
	}
	Log::logger(Log::LogLevel::Debug,
		"NVIDIA compiler file pages: advised_ranges={}, previous_rss_kib={}",
		advised_ranges,
		previous_rss_kib);
#endif
}

}
