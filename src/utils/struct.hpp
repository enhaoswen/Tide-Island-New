#pragma once

#include <cstdint>
#include <limits>
#include <string>

/// @brief Axis-aligned rectangle in surface coordinates.
struct Frame {
	float x_{};
	float y_{};
	float width_{};
	float height_{std::numeric_limits<float>::max()};

	bool operator==(const Frame&) const = default;
};

/// @brief 2D position in surface coordinates.
struct Position {
	float x_{};
	float y_{};
};

/// @brief Normalized source rectangle of a texture.
struct UvRect {
	float u0_{0.f};
	float v0_{0.f};
	float u1_{1.f};
	float v1_{1.f};

	bool operator==(const UvRect&) const = default;
};

/// @brief Opaque handle to a loaded font.
struct FontHandle {
	uint64_t id_{};
};

/// @brief Font family path and pixel size.
struct Font {
	std::string path_{};
	uint16_t    size_{};
};

/// @brief Kind of content shown in an island state.
enum class StateType : uint8_t {
	Clock,
	Lyrics,
	MusicPlayer,
	ControlCenter,
	Osd,
};
