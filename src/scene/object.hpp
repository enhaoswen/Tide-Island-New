#pragma once

#include "utils/struct.hpp"
#include "graphics/texture.hpp"

#include <source_location>
#include <string_view>

namespace Object {

namespace DontUse {
class ClickableItem {
protected:
    ClickCallback callback_left;
    ClickCallback callback_right;

public:
    void set_callback_left(ClickCallback left);
    void set_callback_right(ClickCallback right);
};

}

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

    [[nodiscard]] float radius() const;

    void set_radius(float radius);

    void set_color(const Color& color);

    void add_animation(AnimationDesc anim_desc, std::source_location location = std::source_location::current());

    void update();

    void draw();
};

class Image {
private:
    ImageDesc desc;
    Texture   texture;
    bool      texture_dirty{false};

public:
    explicit Image(const ImageDesc& desc);

    Image(const Image&) = delete;

    Image& operator=(const Image&) = delete;

    [[nodiscard]] const Frame& frame() const;

    void set_frame(const Frame& frame);

    void set_desc(const ImageDesc& desc);

    [[nodiscard]] float radius() const;

    void set_radius(float radius);

    void set_path(std::string_view path);

    void reload();

    void add_animation(AnimationDesc anim_desc, std::source_location location = std::source_location::current());

    void update();

    void draw();
};

class Text {
private:
    TextDesc desc;
    Texture  texture;
    bool     texture_dirty{false};
    float    texture_scale{1.0f};

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

    void add_animation(AnimationDesc anim_desc, std::source_location location = std::source_location::current());

    void update();

    void draw();
};

class ClickableRect : public Rectangle, public DontUse::ClickableItem {
public:
    using Rectangle::Rectangle;

    bool click(float x, float y, bool left);
};

class ClickableImage : public Image, public DontUse::ClickableItem {
public:
    using Image::Image;

    bool click(float x, float y, bool left);
};

class ClickableText : public Text, public DontUse::ClickableItem {
public:
    using Text::Text;

    bool click(float x, float y, bool left);
};

// Returned pointers remain valid until clear().
Rectangle* add_rectangle(const RectDesc& desc);

Image* add_image(const ImageDesc& desc);

Text* add_text(const TextDesc& desc);

ClickableRect* add_clickable_rect(const RectDesc& desc);

ClickableImage* add_clickable_image(const ImageDesc& desc);

ClickableText* add_clickable_text(const TextDesc& desc);

void click(float x, float y, bool left);

void draw();

void clear();

} // namespace Object
