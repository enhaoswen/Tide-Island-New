#pragma once

#include <cstdint>
#include <optional>

#include "backend/sysinfo/sysinfo.hpp"

namespace SysInfo::Detail {

/// @brief Source kind packed into epoll event data.
enum class Tag : uint8_t {
	Pipewire,
	Backlight,
	Power,
};

/// @brief Registers fd with the epoll instance for events; returns true on success.
bool watch(int fd, Tag tag, uint32_t events);

/// @brief Removes fd from the epoll instance.
void unwatch(int fd);

}

namespace SysInfo::Pipewire {

/// @brief Initializes PipeWire and schedules reconnects.
void init();

/// @brief Tears down PipeWire.
void shutdown();

/// @brief Processes pending PipeWire events; returns true if volume changed.
bool dispatch();

/// @brief Returns current volume, or nullopt if unavailable.
std::optional<Volume> get_volume();

}

namespace SysInfo::Backlight {

/// @brief Finds a backlight device and watches its brightness.
void init();

/// @brief Stops watching the backlight device.
void shutdown();

/// @brief Re-reads brightness after a notification; returns true if it changed.
bool dispatch();

/// @brief Returns current brightness 0..1, or nullopt if unavailable.
std::optional<float> get_brightness();

}

namespace SysInfo::Supply {

/// @brief Starts watching kernel power_supply uevents.
void init();

/// @brief Stops watching power_supply uevents.
void shutdown();

/// @brief Drains pending uevents; returns true if power state changed.
bool dispatch();

/// @brief Returns current power state, or nullopt if unavailable.
std::optional<Power> get_power();

}
