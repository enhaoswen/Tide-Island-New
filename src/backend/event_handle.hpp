#pragma once

#include <cstdint>
#include <limits>

namespace Timer {

struct EventHandle {
    std::uint32_t index{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t generation{};
    bool operator==(const EventHandle&) const = default;
};
static_assert(sizeof(EventHandle) == 8);

} // namespace Timer
