#pragma once

#include "cstdint"

#include "utils/struct.hpp"

namespace API {

void init();
const IslandConf& island_state();
void resize(uint32_t width, uint32_t height);
FontHandle load_font(std::string_view family, unsigned pixel_size);
void draw_rectangle(RectDesc&);
void draw_image(ImageDesc&);
void draw_text(TextDesc&);
void to_clock_status();
void clear_status();
void run();

void shutdown();
}