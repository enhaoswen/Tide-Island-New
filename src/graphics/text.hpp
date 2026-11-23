#pragma once

#include "utils/struct.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Text {

// Pixels are top-down, tightly packed, 8-bit coverage values.
// Dimensions, offsets, and advance_x are in raster pixels; divide by the
// draw scale when using them for logical layout.
// (offset_x, offset_y) locates the bitmap's top-left corner relative to
// the text origin on the baseline.
struct Bitmap {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t stride{};
    std::int32_t offset_x{};
    std::int32_t offset_y{};
    float advance_x{};
    std::vector<std::uint8_t> pixels{};

    Bitmap() = default;
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;
    Bitmap(Bitmap&&) = default;
    Bitmap& operator=(Bitmap&&) = default;
};

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
void init();
// pixel_size is the logical font size; draw applies the raster scale.
[[nodiscard]] FontHandle load_font(std::string_view family, unsigned pixel_size,
                                   Weight weight = Weight::Regular);
void unload_font(FontHandle font);
[[nodiscard]] Bitmap draw(std::string_view text, FontHandle font, float scale = 1.0f,
                          float sub_x = 0.0f, float sub_y = 0.0f);
// Font size draw would rasterize at, in 1/64 pixels; 0 if the font or scale is invalid.
[[nodiscard]] unsigned raster_size(FontHandle font, float scale);
void shutdown();

}
