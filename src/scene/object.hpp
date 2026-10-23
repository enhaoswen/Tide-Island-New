#pragma once

#include "graphics/texture.hpp"
#include "utils/struct.hpp"

#include <array>
#include <concepts>
#include <memory>
#include <source_location>
#include <string_view>
#include <vector>

namespace Object {

namespace DontUse {
struct SceneAccess;

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
    friend struct DontUse::SceneAccess;

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

    [[nodiscard]] Frame get_frame() const;

    void add_animation(AnimationDesc anim_desc, std::source_location l = std::source_location::current());

    void update();

    [[nodiscard]] bool has_active_animations() const;
};

// Implementations are compiled once for the supported descriptor types.
extern template class Element<RectDesc>;
extern template class Element<ImageDesc>;
extern template class Element<TextDesc>;
extern template class Element<LayoutDesc>;

class Rectangle : public Element<RectDesc> {
public:
    using Element<RectDesc>::Element;

    void set_desc(const RectDesc& desc);

    void set_radius(float radius);

    void set_color(const Color& color);

    void draw();
};

class Image : public Element<ImageDesc> {
    friend struct DontUse::SceneAccess;

    void prepare();

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
    friend struct DontUse::SceneAccess;

    void prepare();

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

class Layout;

namespace DontUse {

template <typename T>
concept LayoutElement = std::same_as<T, Rectangle> || std::same_as<T, Image> ||
    std::same_as<T, Text> || std::same_as<T, ClickableRect> ||
    std::same_as<T, ClickableImage> || std::same_as<T, ClickableText> ||
    std::same_as<T, Layout>;

}

// Layouts and their members must be created by the scene's add_* factories.
// Membership does not transfer ownership; all handles expire at Object::clear().
// Each group preserves insertion order. Center stays centered on the whole frame;
// sides take space first, losing equal inner portions if they collide. Drawing and
// clicks are clipped to each group's region and every ancestor's region.
class Layout : public Element<LayoutDesc> {
    friend struct DontUse::SceneAccess;

    struct Item {
        std::uint32_t index;
        Align         align;
        bool          auto_size;
    };

    std::vector<Item>    elements;
    std::array<float, 3> lengths{};

    bool add_element_impl(const void* element, Align align);
    bool delete_element_impl(const void* element);
    bool set_auto_size_impl(const void* element, bool enabled);

public:
    explicit Layout(const LayoutDesc& desc);

    void set_desc(const LayoutDesc& desc);

    void set_frame(const Frame& frame);

    void set_spacing(float spacing);

    void set_vertical(bool vertical);

    void set_cross_align(Align align);

    // Existing members keep their insertion order when their group changes.
    // EMPTY_FRAME enables content sizing for images/text, including clickable ones.
    template <DontUse::LayoutElement T>
    bool add_element(T* element, Align align = Align::Left) {
        return add_element_impl(element, align);
    }

    // Detaches without destroying the element or changing its last frame.
    template <DontUse::LayoutElement T>
    bool delete_element(T* element) {
        return delete_element_impl(element);
    }

    // Disable before manually setting or animating a member's width/height.
    template <DontUse::LayoutElement T>
    bool set_auto_size(T* element, bool enabled) {
        return set_auto_size_impl(element, enabled);
    }

    // Updates this subtree's sizes/positions; Object::update advances animations.
    // Parent layouts control member x/y. Frames use absolute scene coordinates.
    void update();
};

// Returned pointers remain valid until clear().
Rectangle* add_rectangle(const RectDesc& desc);

Image* add_image(const ImageDesc& desc);

Text* add_text(const TextDesc& desc);

ClickableRect* add_clickable_rect(const RectDesc& desc);

ClickableImage* add_clickable_image(const ImageDesc& desc);

ClickableText* add_clickable_text(const TextDesc& desc);

Layout* add_layout(const LayoutDesc& desc);

void click(float x, float y, bool left);

void draw();

void update();

bool has_active_animations();

void clear();

} // namespace Object
