#pragma once

#include "graphics/texture.hpp"
#include "utils/struct.hpp"

#include <array>
#include <concepts>
#include <cstdint>
#include <memory>
#include <source_location>
#include <string_view>
#include <stdexcept>
#include <limits>
#include <vector>

namespace Object {

class Rectangle;
class Image;
class Text;
class ClickableRect;
class ClickableImage;
class ClickableText;
class Layout;

namespace DontUse {
struct Key {
    std::uint16_t index{std::numeric_limits<std::uint16_t>::max()};
    std::uint32_t generation{};

    bool operator==(const Key&) const = default;
};

enum class Kind : std::uint8_t {
    Rectangle,
    Image,
    Text,
    ClickableRect,
    ClickableImage,
    ClickableText,
    Layout,
};

template <typename T> constexpr Kind kind() {
    if constexpr (std::same_as<T, Rectangle>)
        return Kind::Rectangle;
    else if constexpr (std::same_as<T, Image>)
        return Kind::Image;
    else if constexpr (std::same_as<T, Text>)
        return Kind::Text;
    else if constexpr (std::same_as<T, ClickableRect>)
        return Kind::ClickableRect;
    else if constexpr (std::same_as<T, ClickableImage>)
        return Kind::ClickableImage;
    else if constexpr (std::same_as<T, ClickableText>)
        return Kind::ClickableText;
    else {
        static_assert(std::same_as<T, Layout>);
        return Kind::Layout;
    }
}

void* resolve(Key key, Kind kind);

struct SceneAccess;
}

// Store handles, not resolved pointers. get() borrows a pointer until deletion/clear.
// Expired handles resolve to nullptr; checked operator-> throws on misuse.
template <typename T>
class Handle {
    DontUse::Key key_{};

    explicit Handle(DontUse::Key key) : key_(key) {
    }

    friend struct DontUse::SceneAccess;
    template <typename> friend class Handle;

public:
    Handle() = default;

    template <typename U> requires std::derived_from<U, T>
    Handle(Handle<U> other) : key_(other.key_) {
    }

    [[nodiscard]] T* get() const { return static_cast<T*>(DontUse::resolve(key_, DontUse::kind<T>())); }
    explicit         operator bool() const { return get() != nullptr; }

    T* operator->() const {
        auto* const value = get();
        if (!value)
            throw std::logic_error("Expired scene handle");
        return value;
    }

    [[nodiscard]] DontUse::Key key() const { return key_; }

    bool operator==(const Handle&) const = default;
};

static_assert(sizeof(Handle<Rectangle>) == 8);

enum class SizeMode : std::uint8_t { Fixed, Content, Fill };

namespace DontUse {
struct SceneAccess;

bool delete_element_impl(Key element);

class ClickableItem {
protected:
    ClickCallback callback_left{};
    ClickCallback callback_right{};

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
    std::uint16_t scene_index{std::numeric_limits<std::uint16_t>::max()};

    void geometry_changed();

    Desc                                     desc{};
    std::vector<std::unique_ptr<Animation> > animations{};

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

    void draw() const;
};

class Image : public Element<ImageDesc> {
    friend struct DontUse::SceneAccess;

    void prepare();

protected:
    Texture texture{};
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
    Texture texture{};
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
// Membership does not transfer ownership; handles expire on deletion or Object::clear().
// Each group preserves insertion order. Center stays centered on the whole frame;
// sides take space first, losing equal inner portions if they collide. Drawing and
// clicks are clipped to each group's region and every ancestor's region.
class Layout : public Element<LayoutDesc> {
    friend struct DontUse::SceneAccess;

    struct Item {
        std::uint16_t index{};
        Align         align{};
        SizeMode      size_mode{};
    };

    std::vector<Item>    elements{};
    std::array<float, 3> lengths{};
    bool                 dirty{true};

    bool add_element_impl(DontUse::Key element, Align align, SizeMode size_mode);

    bool remove_element_impl(DontUse::Key element);

    bool set_size_mode_impl(DontUse::Key element, SizeMode size_mode);

public:
    explicit Layout(const LayoutDesc& desc);

    void set_desc(const LayoutDesc& desc);

    void set_frame(const Frame& frame);

    void set_spacing(float spacing);

    void set_vertical(bool vertical);

    void set_cross_align(Align align);

    // Content measures image/text; Fill uses the layout's full width and height.
    // Changing group or sizing preserves insertion order.
    template <DontUse::LayoutElement T>
    bool add_element(Handle<T> element, Align align = Align::Left, SizeMode size_mode = SizeMode::Fixed) {
        return add_element_impl(element.key(), align, size_mode);
    }

    // Detaches without destroying the element or changing its last frame.
    template <DontUse::LayoutElement T>
    bool remove_element(Handle<T> element) {
        return remove_element_impl(element.key());
    }

    // Use Fixed before manually setting or animating a member's width/height.
    template <DontUse::LayoutElement T>
    bool set_size_mode(Handle<T> element, SizeMode mode) {
        return set_size_mode_impl(element.key(), mode);
    }

    // Updates this subtree's sizes/positions; Object::update advances animations.
    // Parent layouts control member x/y. Frames use absolute scene coordinates.
    void update();
};

// Handles are invalidated immediately by deletion/clear, even inside callbacks.
Handle<Rectangle> add_rectangle(const RectDesc& desc);

Handle<Image> add_image(const ImageDesc& desc);

Handle<Text> add_text(const TextDesc& desc);

Handle<ClickableRect> add_clickable_rect(const RectDesc& desc);

Handle<ClickableImage> add_clickable_image(const ImageDesc& desc);

Handle<ClickableText> add_clickable_text(const TextDesc& desc);

Handle<Layout> add_layout(const LayoutDesc& desc);

void click(float x, float y, bool left);

void draw();

void update();

bool has_active_animations();

// Destroys the element. Layout children are detached and remain in the scene.
// Returns false for an empty or expired handle.
template <DontUse::LayoutElement T>
bool delete_element(Handle<T> element) {
    return DontUse::delete_element_impl(element.key());
}

void clear();

} // namespace Object
