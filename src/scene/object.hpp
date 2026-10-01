#pragma once

#include "utils/struct.hpp"


namespace Object {

void add_rectangle(RectDesc& desc);
void add_image(ImageDesc& desc);
void add_text(TextDesc& desc);
void click(float x, float y, bool left);
void draw();
void clear();
}

