#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace SysInfo {

enum class Update : std::uint8_t {
    None       = 0,
    Volume     = 1,
    Brightness = 2,
    Power      = 4,
};

[[nodiscard]] constexpr Update operator|(Update a, Update b) noexcept {
    return static_cast<Update>(std::to_underlying(a) | std::to_underlying(b));
}

[[nodiscard]] constexpr bool contains(Update set, Update u) noexcept {
    return (std::to_underlying(set) & std::to_underlying(u)) != 0;
}

struct Volume {
    float level{}; // perceptual (cubic, like wpctl/pavucontrol), 1.0 = 100%, may exceed 1
    bool  muted{};
    bool  operator==(const Volume&) const = default;
};

enum class Charge : std::uint8_t {
    Unknown,
    Charging,
    Discharging,
    NotCharging, // plugged but held (charge limit / thresholds)
    Full,
};

struct Power {
    bool               plugged{};
    std::optional<int> battery{}; // percent, nullopt on machines without one
    Charge             status{};
    bool               operator==(const Power&) const = default;
};

void init();
void shutdown();

// Single epoll fd covering every source; add it to the main poll with POLLIN.
[[nodiscard]] int fd();
void              process();

void set_listener(void (*listener)(Update));

// nullopt = source unavailable (no PipeWire / no backlight / no power_supply).
[[nodiscard]] std::optional<Volume> volume();
[[nodiscard]] std::optional<float>  brightness(); // 0..1
// Plug/unplug and status changes are pushed by the kernel; the percentage only refreshes
// when the firmware reports it (most laptops do every few %).
[[nodiscard]] std::optional<Power> power();

}
