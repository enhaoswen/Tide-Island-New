#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Image {

struct Pixels {
    int                       width{};
    int                       height{};
    std::vector<std::uint8_t> rgba{};
};

[[nodiscard]] std::optional<Pixels> decode_square(std::span<const std::uint8_t> encoded, int side);

}
