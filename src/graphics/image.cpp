#include "graphics/image.hpp"

#include "stb_image.h"
#include "utils/log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

using namespace std;

namespace Image {
optional<Image::Pixels> decode_square(span<const uint8_t> encoded, int side) {
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

optional<Pixels> load_strip(const char* path, int frames, int side) {
    int                                                         w{}, h{}, channels{};
    const unique_ptr<unsigned char, decltype(&stbi_image_free)> src{
        stbi_load(path, &w, &h, &channels, 4),
        &stbi_image_free
    };
    if (!src) {
        Log::logger(Log::Warning, R"_(Failed to load image path="{}": {})_", path, stbi_failure_reason());
        return nullopt;
    }
    if (frames <= 0 || side <= 0 || w != h * frames) {
        Log::logger(Log::Warning, R"_(Not a strip of {} square frames, path="{}")_", frames, path);
        return nullopt;
    }

    const int cell = h;
    const int out  = min(side, cell);
    const int ow   = out * frames;

    struct Tap {
        int   i;
        float w;
    };
    vector<vector<Tap> > taps(static_cast<size_t>(out));
    const float          ratio = static_cast<float>(cell) / static_cast<float>(out);
    for (int o = 0; o < out; ++o) {
        const float a = static_cast<float>(o) * ratio, b = a + ratio;
        for (int i = static_cast<int>(a); static_cast<float>(i) < b && i < cell; ++i) {
            const float cover = min(b, static_cast<float>(i + 1)) - max(a, static_cast<float>(i));
            if (cover > 0.f)
                taps[static_cast<size_t>(o)].push_back({i, cover / ratio});
        }
    }

    vector<float> pre(static_cast<size_t>(cell) * cell * 4);
    vector<float> mid(static_cast<size_t>(out) * cell * 4);
    Pixels        p{ow, out, vector<uint8_t>(static_cast<size_t>(ow) * out * 4)};
    for (int f = 0; f < frames; ++f) {
        for (int y = 0; y < cell; ++y)
            for (int x = 0; x < cell; ++x) {
                const uint8_t* s = src.get() + (static_cast<size_t>(y) * w + f * cell + x) * 4;
                float*         d = &pre[(static_cast<size_t>(y) * cell + x) * 4];
                const float    a = s[3] / 255.f;
                for (int c = 0; c < 3; ++c)
                    d[c] = s[c] / 255.f * a;
                d[3] = a;
            }

        ranges::fill(mid, 0.f);
        for (int y = 0; y < cell; ++y)
            for (int o = 0; o < out; ++o) {
                float* d = &mid[(static_cast<size_t>(y) * out + o) * 4];
                for (const Tap& t : taps[static_cast<size_t>(o)]) {
                    const float* s = &pre[(static_cast<size_t>(y) * cell + t.i) * 4];
                    for (int c = 0; c < 4; ++c)
                        d[c] += s[c] * t.w;
                }
            }

        for (int o = 0; o < out; ++o)
            for (int x = 0; x < out; ++x) {
                array<float, 4> acc{};
                for (const Tap& t : taps[static_cast<size_t>(o)]) {
                    const float* s = &mid[(static_cast<size_t>(t.i) * out + x) * 4];
                    for (size_t c = 0; c < 4; ++c)
                        acc[c] += s[c] * t.w;
                }
                uint8_t* d = &p.rgba[(static_cast<size_t>(o) * ow + f * out + x) * 4];
                for (size_t c = 0; c < 3; ++c)
                    d[c] = acc[3] > 0.f ? static_cast<uint8_t>(lround(clamp(acc[c] / acc[3], 0.f, 1.f) * 255.f)) : 0;
                d[3] = static_cast<uint8_t>(lround(clamp(acc[3], 0.f, 1.f) * 255.f));
            }
    }
    return p;
}

}