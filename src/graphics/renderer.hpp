#pragma once

#include "utils/struct.hpp"

class Texture;

namespace Renderer {

void init();

// Destroy scene objects before calling shutdown, while EGL is still current.
void shutdown();

void begin_frame();

void end_frame();

// Logical scene coordinates, clipped to the surface. Call inside a render pass.
void set_clip(const Frame& frame);

void prepare_image(const ImageDesc& desc, Texture& texture);

void prepare_text(const TextDesc& desc, Texture& texture);

void draw_rectangle(const RectDesc& desc);

void draw_image(const ImageDesc& desc, const Texture& texture);

void draw_text(const TextDesc& desc, const Texture& texture);

std::pair<float, float> actual_size(ImageDesc desc);

std::pair<float, float> actual_size(TextDesc desc);

}