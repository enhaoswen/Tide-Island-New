#pragma once

#include "backend/sysinfo/sysinfo.hpp"

#include <cstdint>
#include <optional>

namespace SysInfo::Detail {

enum class Tag : std::uint8_t {
    Pipewire,
    Backlight,
    Power,
};

bool watch(int fd, Tag tag, std::uint32_t events);
void unwatch(int fd);

}

namespace SysInfo::Pipewire {

void                  init();
void                  shutdown();
bool                  dispatch();
std::optional<Volume> volume();

}

namespace SysInfo::Backlight {

void                 init();
void                 shutdown();
bool                 dispatch();
std::optional<float> brightness();

}

namespace SysInfo::Supply {

void                 init();
void                 shutdown();
bool                 dispatch();
std::optional<Power> power();

}
