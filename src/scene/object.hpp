#pragma once

#include "graphics/texture.hpp"
#include "utils/color.hpp"
#include "utils/struct.hpp"

#include <cstdint>
#include <functional>
#include <source_location>

/* ARCHITECTURE

1. we use template<> Handle to refer to object
instead of it's pointer.

2. we use object_entry to save void* elements
so that we don't need to know the type while we
run the program.

*/

namespace Object {

namespace Detail {

template <typename T>
concept has_radius = requires(T t) { { t.radius } -> std::same_as<float&>; };

template <typename T>
concept has_color = requires(T t) { { t.color } -> std::same_as<Color&>; };

struct Rectangle {
    Frame frame{};
    Color color{};
    float radius{};
};

struct Image {
    Frame   frame{};
    Texture texture{};
    float   radius{};
    bool    fix{true};
    bool    have_parent{false};
};

struct Text {
    Frame      frame{};
    Texture    texture{};
    FontHandle font{};
    Color      color{};
    bool       fit{true};
    bool       have_parent{false};
};

template <typename T>
concept is_element =
    std::same_as<decltype(std::declval<T>().info), Rectangle> ||
    std::same_as<decltype(std::declval<T>().info), Image> ||
    std::same_as<decltype(std::declval<T>().info), Text> ||
    std::same_as<decltype(std::declval<T>().info), LayoutDesc>; // TODO

template <typename T>
class Base {
protected:
    T info;

    float* animation_target(AnimationTarget target);

public:
    explicit Base(const T& arg_desc);

    std::pair<float, float> actual_size()
        requires requires(T t) { { t.info.fix } -> std::same_as<bool&>; };

    void add_animation(AnimationDesc animation_desc);

    void set_radius(float radius, std::source_location l = std::source_location::current())
        requires has_radius<T>;

    void set_radius(float radius, std::source_location l = std::source_location::current());

    void set_color(Color color, std::source_location l = std::source_location::current())
        requires has_color<T>;

    void set_color(Color color, std::source_location l = std::source_location::current());
};

class Interactive {
private:
    std::function<void()> lcb;
    std::function<void()> rcb;

public:

};

template <typename T, typename... Features>
    requires is_element<T>
class Element : public T, public Features... {

    bool click(float x, float y, bool left)
        requires (requires(Features f) { f.lcb; } && ...);

};

}

using Rectangle          = Detail::Element<Detail::Base<Detail::Rectangle> >;
using Image              = Detail::Element<Detail::Base<Detail::Image> >;
using Text               = Detail::Element<Detail::Base<Detail::Text> >;
using ClickableRectangle = Detail::Element<Detail::Base<Detail::Rectangle>, Detail::Interactive>;
using ClickableImage     = Detail::Element<Detail::Base<Detail::Image>, Detail::Interactive>;
using ClickableText      = Detail::Element<Detail::Base<Detail::Text>, Detail::Interactive>;

class Layout : public Detail::Base<LayoutDesc> {
private:

public:

};

class Handle {
private:
    void*    handle;
    uint16_t gen;

public:
    explicit Handle(void* arg_handle, const uint16_t arg_gen) : handle(arg_handle), gen(arg_gen) {
    }
};

Handle add_rectangle(RectDesc desc);

Handle add_image(ImageDesc desc);

Handle add_text(TextDesc desc);

Handle add_layout(LayoutDesc desc);

Handle add_clickable_rectangle(RectDesc desc);

Handle add_clickable_image(ImageDesc desc);

Handle add_clickable_text(TextDesc desc);

Handle add_clickable_layout(LayoutDesc desc);

void delete_handle(Handle handle);

}