#pragma once

#include <cstdint>
#include <span>

namespace Renderer {
struct TextureAccess;
}

// pimpl
class Texture {
private:
    struct Impl;
    Impl* impl{};
    friend struct Renderer::TextureAccess;
    std::uint32_t view_id() const;
public:
    enum class Format : char{
        R8,
        RGBA8,
    };

    Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept : impl(other.impl) {
        other.impl = nullptr;
    }

    ~Texture();

    // Release GPU resources before shutting down Sokol or the EGL context.
    void reset();
    // Pixels must be tightly packed. CPU storage is borrowed only during upload.
    void upload(int width, int height, Format format,
                std::span<const std::uint8_t> pixels);

    bool valid() const;
    int width() const;
    int height() const;
};
