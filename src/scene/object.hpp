#pragma once

#include "graphics/texture.hpp"
#include "utils/color.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"

#include <cstdint>
#include <deque>
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

class Handle {
private:
    uint16_t id;
    uint16_t gen;

public:
    explicit Handle(uint16_t arg_id, const uint16_t arg_gen) : id(arg_id), gen(arg_gen) {
    }
};

namespace Detail {

struct Rectangle {
    Frame frame{};
    Color color{};
    float radius{};
    bool  dirty{true};
};

struct Image {
    Frame   frame{};
    Texture texture{};
    float   radius{};
    bool    fix{true};
    bool    have_parent{false};
    bool    dirty{true};
};

struct Text {
    Frame      frame{};
    Texture    texture{};
    FontHandle font{};
    Color      color{};
    bool       fit{true};
    bool       have_parent{false};
    bool       dirty{true};
};

struct Layout {
    Frame               frame{};
    std::vector<Handle> children{};
    float               spacing{};
    bool                vertical{false};
    Align               align{Align::Left};
    bool                dirty{true};
};

template <typename T>
concept has_radius = requires(T t) { { t.radius } -> std::same_as<float&>; };

template <typename T>
concept has_color = requires(T t) { { t.color } -> std::same_as<Color&>; };


template <typename T>
concept is_element =
    std::same_as<decltype(std::declval<T>().info), Rectangle> ||
    std::same_as<decltype(std::declval<T>().info), Image> ||
    std::same_as<decltype(std::declval<T>().info), Text> ||
    std::same_as<decltype(std::declval<T>().info), Layout>;

void add_animation(AnimationDesc animation_desc, float* target_ptr);

template <typename T>
class Base {
protected:
    T info;

    float* animation_target(AnimationTarget target) {
        switch (target) {
        case AnimationTarget::Width:
            return &info.frame.width;
        case AnimationTarget::Height:
            return &info.frame.height;
        case AnimationTarget::X:
            return &info.frame.x;
        case AnimationTarget::Y:
            return &info.frame.y;

        case AnimationTarget::Radius:
            if constexpr (requires { info.radius; })
                return &info.radius;
            return nullptr;

        case AnimationTarget::ColorR:
        case AnimationTarget::ColorG:
        case AnimationTarget::ColorB:
        case AnimationTarget::ColorA:
            if constexpr (requires { info.color; }) {
                const auto index = static_cast<unsigned>(target)
                    - static_cast<unsigned>(AnimationTarget::ColorR);
                return &info.color[index];
            }
            return nullptr;
        default:
            return nullptr;
        }
    }

public:
    explicit Base(const T& arg_desc) {
        info = arg_desc;
    }

    std::pair<float, float> actual_size()
        requires requires(T t) { { t.info.fix } -> std::same_as<bool&>; } {
        return {this->info.frame.width, this->info.frame.height};
    }

    void add_animation(AnimationDesc animation_desc) {
        Detail::add_animation(animation_desc, animation_target(animation_desc.target));
    }

    void set_radius(float radius, std::source_location l = std::source_location::current())
        requires has_radius<T> {
        info.radius = radius;
    }

    void set_radius(float, std::source_location l = std::source_location::current()) {
        Log::logger(
            Log::Warning,
            "{}:{}: {} does not support function set_radius",
            l.file_name(),
            l.line(),
            info.radius, Log::type_name<T>()
            );
    }

    void set_color(Color color, std::source_location l = std::source_location::current())
        requires has_color<T> {
        info.color = color;
    }

    void set_color(Color, std::source_location l = std::source_location::current()) {
        Log::logger(
            Log::Warning,
            "{}:{}: {} does not support function set_color",
            l.file_name(),
            l.line(),
            info.radius, Log::type_name<T>()
            );
    }

    [[nodiscard]] bool dirty() { return info.dirty; }

    [[nodiscard]] float left() { return info.frame.x; }
    [[nodiscard]] float right() { return info.frame.x + info.frame.width; }
    [[nodiscard]] float top() { return info.frame.y; }
    [[nodiscard]] float bottom() { return info.frame.y + info.frame.height; }

};

class Interactive {
private:
    std::function<void()> lcb;
    std::function<void()> rcb;
};

template <typename T, typename... Features>
    requires is_element<T>
class Element : public T, public Features... {

    bool click(float x, float y, bool left)
        requires (requires(Features f) { f.lcb; } && ...);

};

template <typename T>
class Pool {
private:
    std::deque<T> elements;
    std::vector<uint16_t> free;

public:

    template <typename... A>
    std::pair<uint32_t, uint16_t> emplace(A&&... a) {
        uint32_t i;
        if (free.empty()) {
            i = static_cast<uint32_t>(elements.size());
            elements.emplace_back();
        } else {
            i = free.back();
            free.pop_back();
        }
        elements[i].value.emplace(std::forward<A>(a)...);
        return {i, elements[i].gen};
    }

    T* get(uint32_t i, uint16_t g) {
        if (i >= elements.size()) return nullptr;
        auto& s = elements[i];
        return (s.gen == g) ? &*s.value : nullptr;
    }

    void erase(uint32_t i, uint16_t g) {
        if (!get(i, g)) return;
        elements[i].value.reset();
        ++elements[i].gen;
        free.push_back(i);
    }
};

template <typename T>
Pool<T> pool;

}

using Rectangle          = Detail::Element<Detail::Base<Detail::Rectangle> >;
using Image              = Detail::Element<Detail::Base<Detail::Image> >;
using Text               = Detail::Element<Detail::Base<Detail::Text> >;
using ClickableRectangle = Detail::Element<Detail::Base<Detail::Rectangle>, Detail::Interactive>;
using ClickableImage     = Detail::Element<Detail::Base<Detail::Image>, Detail::Interactive>;
using ClickableText      = Detail::Element<Detail::Base<Detail::Text>, Detail::Interactive>;

class Layout : public Detail::Base<Detail::Layout> {
private:

public:
    // always add to the left
    void add_children(Handle id) { this->info.children.push_back(id); }

    void update();
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