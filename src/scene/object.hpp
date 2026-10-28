#pragma once

#include "utils/color.hpp"
#include "utils/struct.hpp"

#include <source_location>
#include <functional>

/* ARCHITECTURE

1. we use template<> Handle to refer to object
instead of it's pointer.

2. we use object_entry to save void* elements
so that we don't need to know the type while we
run the program.

*/

namespace Object {

template <typename T>
concept has_radius = requires(T t) { { t.radius } -> std::same_as<float&>; };

template <typename T>
concept has_color = requires(T t) { { t.color } -> std::same_as<Color&>; };

template <typename T>
concept is_element =
    std::same_as<decltype(std::declval<T>().desc), RectDesc> ||
    std::same_as<decltype(std::declval<T>().desc), ImageDesc> ||
    std::same_as<decltype(std::declval<T>().desc), TextDesc> ||
    std::same_as<decltype(std::declval<T>().desc), LayoutDesc>;


template <typename T>
class Base {
private:
    T desc;

    float* animation_target(AnimationTarget target);

public:
    explicit Base(const T& arg_desc);

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
class Element : public Features... {

    bool click(float x, float y, bool left)
        requires (requires(Features f) { f.lcb; } && ...);

};

using Rectangle          = Element<Base<RectDesc> >;
using Image              = Element<Base<ImageDesc> >;
using Text               = Element<Base<TextDesc> >;
using Layout             = Element<Base<LayoutDesc> >;
using ClickableRectangle = Element<Base<RectDesc>, Interactive>;
using ClickableImage     = Element<Base<ImageDesc>, Interactive>;
using ClickableText      = Element<Base<TextDesc>, Interactive>;
using ClickableLayout    = Element<Base<LayoutDesc>, Interactive>;

}