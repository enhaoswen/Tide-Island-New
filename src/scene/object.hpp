#pragma once

#include "utils/struct.hpp"
#include "graphics/texture.hpp"

#include <source_location>
#include <string_view>

namespace Object {

class Rectangle {
private:
    RectDesc desc;

public:
    explicit Rectangle(const RectDesc& desc);
    Rectangle(const Rectangle&) = delete;
    Rectangle& operator=(const Rectangle&) = delete;

    [[nodiscard]] const Frame& frame() const;
    void set_frame(const Frame& frame);
    void set_desc(const RectDesc& desc);
    void set_radius(float radius);
    void set_color(const Color& color);

    void set_click_callback_left(void (*left)());
    void set_click_callback_right(void (*right)());
    void add_animation(AnimationDesc anim_desc, std::source_location location = std::source_location::current());
    bool click(float x, float y, bool left);
    void update();
    void draw();
};

class Image {
private:
    ImageDesc desc;
    Texture texture;
    bool texture_dirty{false};

public:
    explicit Image(const ImageDesc& desc);
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    [[nodiscard]] const Frame& frame() const;
    void set_frame(const Frame& frame);
    void set_desc(const ImageDesc& desc);
    void set_radius(float radius);
    void set_path(std::string_view path);
    void reload();

    void set_click_callback_left(void (*left)());
    void set_click_callback_right(void (*right)());
    void add_animation(AnimationDesc anim_desc, std::source_location location = std::source_location::current());
    bool click(float x, float y, bool left);
    void update();
    void draw();
};

class Text {
private:
    TextDesc desc;
    Texture texture;
    bool texture_dirty{false};
    float texture_scale{1.0f};

public:
    explicit Text(const TextDesc& desc);
    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;

    [[nodiscard]] const Frame& frame() const;
    void set_frame(const Frame& frame);
    void set_desc(const TextDesc& desc);
    void set_color(const Color& color);
    void set_text(std::string_view text);
    void set_font(FontHandle font);

    void set_click_callback_left(void (*left)());
    void set_click_callback_right(void (*right)());
    void add_animation(AnimationDesc anim_desc, std::source_location location = std::source_location::current());
    bool click(float x, float y, bool left);
    void update();
    void draw();
};

// Returned pointers remain valid until clear().
Rectangle* add_rectangle(const RectDesc& desc);
Image* add_image(const ImageDesc& desc);
Text* add_text(const TextDesc& desc);
void click(float x, float y, bool left);
void draw();
void clear();

} // namespace Object
