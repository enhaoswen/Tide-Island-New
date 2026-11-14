#include "graphics/image.hpp"

#include "stb_image.h"
#include "utils/log.hpp"

#include <algorithm>
#include <limits>
#include <memory>

using namespace std;

optional<Image::Pixels> Image::decode_square(span<const uint8_t> encoded, int side) {
    if (encoded.empty() || side <= 0 || encoded.size() > static_cast<size_t>(numeric_limits<int>::max()))
        return nullopt;

    int                                                         w{}, h{}, channels{};
    const unique_ptr<unsigned char, decltype(&stbi_image_free)> src{
        stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &w, &h, &channels, 4),
        &stbi_image_free
    };
    if (!src) {
        Log::logger(Log::Warning, "Failed to decode image: {}", stbi_failure_reason());
        return nullopt;
    }

    const int crop = min(w, h);
    const int x0   = (w - crop) / 2;
    const int y0   = (h - crop) / 2;
    const int out  = min(side, crop);

    Pixels p{out, out, vector<uint8_t>(static_cast<size_t>(out) * out * 4)};
    for (int oy = 0; oy < out; ++oy) {
        const int sy0 = y0 + oy * crop / out;
        const int sy1 = max(sy0 + 1, y0 + (oy + 1) * crop / out);
        for (int ox = 0; ox < out; ++ox) {
            const int sx0 = x0 + ox * crop / out;
            const int sx1 = max(sx0 + 1, x0 + (ox + 1) * crop / out);

            uint32_t sum[4]{};
            for (int sy = sy0; sy < sy1; ++sy) {
                const uint8_t* row = src.get() + (static_cast<size_t>(sy) * w + sx0) * 4;
                for (int sx = sx0; sx < sx1; ++sx, row += 4)
                    for (int c = 0; c < 4; ++c)
                        sum[c] += row[c];
            }
            const uint32_t n   = static_cast<uint32_t>((sy1 - sy0) * (sx1 - sx0));
            uint8_t*       dst = p.rgba.data() + (static_cast<size_t>(oy) * out + ox) * 4;
            for (int c = 0; c < 4; ++c)
                dst[c] = static_cast<uint8_t>((sum[c] + n / 2) / n);
        }
    }
    return p;
}
