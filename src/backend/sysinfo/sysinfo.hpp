#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace SysInfo {

/// @brief Bitmask of sources that changed in a single update.
enum class Update : uint8_t {
	None       = 0,
	Volume     = 1,
	Brightness = 2,
	Power      = 4,
};

/// @brief Combines update flags.
[[nodiscard]] constexpr Update operator|(Update a, Update b) {
	return static_cast<Update>(std::to_underlying(a) | std::to_underlying(b));
}

/// @brief Returns true if flag u is set in set.
[[nodiscard]] constexpr bool has_flag(Update set, Update u) {
	return (std::to_underlying(set) & std::to_underlying(u)) != 0;
}

/// @brief Current audio output volume.
struct Volume {
	float level_{}; // perceptual (cubic, like wpctl/pavucontrol), 1.0 = 100%, may exceed 1
	bool  is_muted_{};
	/// @brief Compares level and mute state.
	bool operator==(const Volume&) const = default;
};

/// @brief Battery charge state.
enum class Charge : uint8_t {
	Unknown,
	Charging,
	Discharging,
	NotCharging, // plugged but held (charge limit / thresholds)
	Full,
};

/// @brief Current power supply state.
struct Power {
	bool               is_plugged_{};
	std::optional<int> battery_{}; // percent, nullopt on machines without one
	Charge             status_{};
	/// @brief Compares all fields.
	bool operator==(const Power&) const = default;
};

/// @brief Initializes all sources and the epoll fd.
void init();

/// @brief Shuts down all sources and closes the epoll fd.
void shutdown();

// Single epoll fd covering every source; add it to the main poll with POLLIN.
/// @brief Returns the epoll fd to poll for readiness.
[[nodiscard]] int fd();

/// @brief Processes ready sources and notifies the listener of changes.
void process();

/// @brief Registers the listener invoked with changed sources after process().
void set_listener(void (*listener)(Update));

// nullopt = source unavailable (no PipeWire / no backlight / no power_supply).
/// @brief Returns current audio volume, or nullopt if unavailable.
[[nodiscard]] std::optional<Volume> get_volume();

/// @brief Returns current backlight brightness 0..1, or nullopt if unavailable.
[[nodiscard]] std::optional<float> get_brightness();

// Plug/unplug and status changes are pushed by the kernel; the percentage only refreshes
// when the firmware reports it (most laptops do every few %).
/// @brief Returns current power state, or nullopt if unavailable.
[[nodiscard]] std::optional<Power> get_power();

}
