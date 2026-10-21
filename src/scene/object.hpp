#pragma once

#include "utils/struct.hpp"
#include "graphics/texture.hpp"

#include <memory>
#include <source_location>
#include <string_view>
#include <vector>

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

// Elements stay at a fixed address because animations point into desc.
template <typename Desc>
class Element {
protected:
    Desc                                     desc;
    std::vector<std::unique_ptr<Animation> > animations;

    ~Element() = default;

    void replace_desc(const Desc& arg_desc);

public:
    explicit Element(const Desc& arg_desc);

    Element(const Element&) = delete;

    Element& operator=(const Element&) = delete;

    Element(Element&&) = delete;

    Element& operator=(Element&&) = delete;

    void set_frame(const Frame& frame);

    void add_animation(AnimationDesc anim_desc, std::source_location l = std::source_location::current());

    void update();

    [[nodiscard]] bool has_active_animations() const;
};

// Implementations are compiled once for the supported descriptor types.
extern template class Element<RectDesc>;
extern template class Element<ImageDesc>;
extern template class Element<TextDesc>;

class Rectangle : public Element<RectDesc> {
public:
    using Element<RectDesc>::Element;

    void set_desc(const RectDesc& desc);

    void set_radius(float radius);

    void set_color(const Color& color);

    void draw();
};

class Image : public Element<ImageDesc> {
protected:
    Texture texture;
    bool    texture_dirty{false};

public:
    explicit Image(const ImageDesc& desc);

    void set_desc(const ImageDesc& desc);

    void set_radius(float radius);

    void set_path(std::string_view path);

    void reload();

    void draw();
};

class Text : public Element<TextDesc> {
protected:
    Texture texture;
    bool    texture_dirty{false};
    float   texture_scale{1.0f};

public:
    explicit Text(const TextDesc& desc);

    void set_desc(const TextDesc& desc);

    void set_color(const Color& color);

    void set_text(std::string_view text);

    void set_font(FontHandle font);

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

void update();

bool has_active_animations();

void clear();

} // namespace Object