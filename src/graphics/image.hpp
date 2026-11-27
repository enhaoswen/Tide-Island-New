#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Image {

/// @brief Decoded 8-bit RGBA pixels, top-down, tightly packed.
struct Pixels {
	int                  width_{};
	int                  height_{};
	std::vector<uint8_t> rgba_{};
};

/// @brief Decodes an encoded image and center-crops it to a `side` x `side` square, area-averaged down.
[[nodiscard]] std::optional<Pixels> decode_square(std::span<const uint8_t> encoded, int side);

/// @brief Loads a horizontal strip of `frames` square frames from `path`, area-averaged down to `side` px.
[[nodiscard]] std::optional<Pixels> load_strip(const char* path, int frames, int side);

}
