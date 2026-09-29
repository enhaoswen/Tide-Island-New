#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace Text {

struct FontHandle {
    std::uint64_t id{};
};

// Pixels are top-down, tightly packed, 8-bit coverage values.
// (offset_x, offset_y) locates the bitmap's top-left corner relative to
// the text origin on the baseline.
struct Bitmap {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t stride{};
    std::int32_t offset_x{};
    std::int32_t offset_y{};
    float advance_x{};
    std::vector<std::uint8_t> pixels;

    Bitmap() = default;
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;
    Bitmap(Bitmap&&) = default;
    Bitmap& operator=(Bitmap&&) = default;
};

// Call init before loading fonts; unload them before shutdown.
// Text's font state is used from one thread at a time. A draw that first
// encounters a missing script may perform a font lookup, so prepare lyrics
// before they become visible.
void init();
[[nodiscard]] FontHandle load_font(std::string_view family, unsigned pixel_size);
void unload_font(FontHandle font);
[[nodiscard]] Bitmap draw(std::string_view text, FontHandle font);
void shutdown();

}