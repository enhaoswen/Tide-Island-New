#pragma once

#include "graphics/texture.hpp"
#include "utils/color.hpp"
#include "utils/struct.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace Renderer {

void init();

// Destroy scene objects before calling shutdown, while EGL is still current.
void shutdown();

void begin_frame();

void end_frame();

// Content -> texture. String / path handling lives here (not in the scene
// graph), because font rasterization and image decoding are renderer concerns.
// Text is rasterized at the surface scale and tagged with it so the scene graph
// can recover logical dimensions for layout.
Texture prepare_image(std::string_view path);
// Decodes an encoded image (jpeg / png / ...) from memory. Returns an invalid
// texture on failure instead of aborting, since the bytes come from outside.
Texture decode_image(std::span<const std::uint8_t> encoded);

// Rasterization scale for text (physical pixels per logical pixel).
float text_scale();

Texture prepare_text(std::string_view text, FontHandle font);

// Satisfies Object::Renderer (see scene/render.hpp). Stateless: all GL state is
// internal to the renderer translation unit, so a single instance is enough.
struct GpuRenderer {
    void draw_rect(const Frame& frame, const Color& color, float radius, float opacity);

    void draw_image(const Frame& frame, const Texture& texture, float radius, float opacity);

    void draw_text(const Frame& frame, const Texture& texture, FontHandle font, const Color& color, float opacity);

    void push_clip(const Frame& frame, float radius);

    void pop_clip();
};

} // namespace Renderer
