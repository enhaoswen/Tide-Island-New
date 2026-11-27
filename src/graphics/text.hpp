#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "utils/struct.hpp"

namespace Text {

// Pixels are top-down, tightly packed, 8-bit coverage values.
// Dimensions, offsets, and advance_x are in raster pixels; divide by the
// draw scale when using them for logical layout.
// (offset_x, offset_y) locates the bitmap's top-left corner relative to
// the text origin on the baseline.
/// @brief Rendered text bitmap: tightly packed 8-bit coverage plus layout metrics.
struct Bitmap {
	uint32_t             width_{};
	uint32_t             height_{};
	uint32_t             stride_{};
	int32_t              offset_x_{};
	int32_t              offset_y_{};
	float                advance_x_{};
	std::vector<uint8_t> pixels_{};

	Bitmap()                         = default;
	Bitmap(const Bitmap&)            = delete;
	Bitmap& operator=(const Bitmap&) = delete;
	Bitmap(Bitmap&&)                 = default;
	Bitmap& operator=(Bitmap&&)      = default;
};

/// @brief Font weight requested from fontconfig.
enum class Weight : uint8_t {
	Regular,
	Medium,
	DemiBold,
	Bold,
};

// Call init before loading fonts; unload them before shutdown.
// Text's font state is used from one thread at a time. A draw that first
// encounters a missing script may perform a font lookup, so prepare lyrics
// before they become visible.
/// @brief Initializes the FreeType library; call before loading fonts.
void init();
// pixel_size is the logical font size; draw applies the raster scale.
/// @brief Loads a font for `family` at `pixel_size` logical pixels with `weight`.
[[nodiscard]] FontHandle load_font(std::string_view family, unsigned pixel_size, Weight weight = Weight::Regular);
/// @brief Unloads a previously loaded font.
void unload_font(FontHandle font);
/// @brief Renders `text` in `font` at `scale` with subpixel offsets `sub_x`/`sub_y`.
[[nodiscard]] Bitmap
draw(std::string_view text, FontHandle font, float scale = 1.f, float sub_x = 0.f, float sub_y = 0.f);
// Font size draw would rasterize at, in 1/64 pixels; 0 if the font or scale is invalid.
/// @brief Font size `draw` would rasterize at, in 1/64 pixels; 0 if font or scale is invalid.
[[nodiscard]] unsigned raster_size(FontHandle font, float scale);
/// @brief Releases all loaded fonts and the FreeType library.
void shutdown();

}
