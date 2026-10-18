#pragma once

#include "cstdint"

#include "utils/struct.hpp"
#include "scene/object.hpp"

namespace API {

void init();
const IslandConf& island_state();
void resize(uint32_t width, uint32_t height);
FontHandle load_font(std::string_view family, unsigned pixel_size);
// Returned pointers belong to Object and are invalidated by clear_status().
Object::Rectangle* add_rectangle(const RectDesc& desc);
Object::Image* add_image(const ImageDesc& desc);
Object::Text* add_text(const TextDesc& desc);
Object::ClickableRect* add_clickable_rect(const RectDesc& desc);
Object::ClickableImage* add_clickable_image(const ImageDesc& desc);
Object::ClickableText* add_clickable_text(const TextDesc& desc);
void push_event(std::chrono::microseconds duration, void (*callback)(), float priority);
void clear_status();
void run();

void shutdown();
}
