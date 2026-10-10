#pragma once

#include "utils/struct.hpp"

namespace Renderer {

void init();
void begin_frame();
void end_frame();

void draw_rectangle(const RectDesc& desc);
void draw_image(const ImageDesc& desc);
void draw_text(const TextDesc& desc);
}
