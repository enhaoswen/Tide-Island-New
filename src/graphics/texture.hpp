#pragma once

#include <cstdint>
#include <span>

namespace Renderer {
struct TextureAccess;
}

class Texture {
private:
    struct Impl;
    Impl* impl{};

public:
    enum class Format : std::uint8_t {
        R8,
        RGBA8,
    };

    Texture();

    Texture(const Texture&) = delete;

    Texture& operator=(const Texture&) = delete;

    Texture& operator=(Texture&& other) noexcept {
        std::swap(impl, other.impl);
        return *this;
    }

    Texture(Texture&& other) noexcept : impl(other.impl) { other.impl = nullptr; }

    ~Texture();

    void reset();

    [[nodiscard]] std::uint32_t view_id() const;

    void upload(int width, int height, Format format, std::span<const std::uint8_t> pixels);

    // Content rasterized for high-DPI is uploaded at scale (physical size), while
    // the scene graph lays out in logical size. Record the ratio so callers can
    // recover the logical extent; defaults to 1.0 (logical == physical).
    void set_scale(float scale) noexcept;

    [[nodiscard]] float scale() const noexcept;

    [[nodiscard]] bool valid() const;

    [[nodiscard]] int width() const;

    [[nodiscard]] int height() const;

    // Physical size divided by scale; used by layout, not by the GPU.
    [[nodiscard]] float logical_width() const noexcept;

    [[nodiscard]] float logical_height() const noexcept;
};