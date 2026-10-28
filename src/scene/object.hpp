#pragma once

#include "utils/color.hpp"
#include "utils/struct.hpp"

#include <source_location>
#include <functional>
#include <cstdint>

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

template <typename T>
concept is_element =
    std::same_as<decltype(std::declval<T>().desc), RectDesc> ||
    std::same_as<decltype(std::declval<T>().desc), ImageDesc> ||
    std::same_as<decltype(std::declval<T>().desc), TextDesc> ||
    std::same_as<decltype(std::declval<T>().desc), LayoutDesc>;


template <typename T>
class Base {
protected:
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
class Element : public T, public Features... {

    bool click(float x, float y, bool left)
        requires (requires(Features f) { f.lcb; } && ...);

};

}

using Rectangle          = Detail::Element<Detail::Base<RectDesc> >;
using Image              = Detail::Element<Detail::Base<ImageDesc> >;
using Text               = Detail::Element<Detail::Base<TextDesc> >;
using Layout             = Detail::Element<Detail::Base<LayoutDesc> >;
using ClickableRectangle = Detail::Element<Detail::Base<RectDesc>, Detail::Interactive>;
using ClickableImage     = Detail::Element<Detail::Base<ImageDesc>, Detail::Interactive>;
using ClickableText      = Detail::Element<Detail::Base<TextDesc>, Detail::Interactive>;
using ClickableLayout    = Detail::Element<Detail::Base<LayoutDesc>, Detail::Interactive>;

template <typename T>
    requires Detail::is_element<T>
class Handle {
private:
    T*       handle;
    uint16_t gen;

public:
    explicit Handle(T* arg_handle, const uint16_t arg_gen) : handle(arg_handle), gen(arg_gen) {
    }
};

Handle<Rectangle> add_rectangle(RectDesc desc);

Handle<Image> add_image(ImageDesc desc);

Handle<Text> add_text(TextDesc desc);

Handle<Layout> add_layout(LayoutDesc desc);

Handle<ClickableRectangle> add_clickable_rectangle(RectDesc desc);

Handle<ClickableImage> add_clickable_image(ImageDesc desc);

Handle<ClickableText> add_clickable_text(TextDesc desc);

Handle<ClickableLayout> add_clickable_layout(LayoutDesc desc);

template <typename T>
void delete_handle(Handle<T> handle);

}