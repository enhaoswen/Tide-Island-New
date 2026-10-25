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
public:
    enum class Format : std::uint8_t {
        R8,
        RGBA8,
    };

    Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept : impl(other.impl) { other.impl = nullptr; }
    ~Texture();

    void reset();
    [[nodiscard]] std::uint32_t view_id() const;

    void upload(int width, int height, Format format, std::span<const std::uint8_t> pixels);

    [[nodiscard]] bool valid() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;

};
