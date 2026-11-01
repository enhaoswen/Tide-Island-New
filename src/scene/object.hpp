#pragma once

#include "graphics/texture.hpp"
#include "utils/color.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <source_location>

namespace Object {

class Handle {
public:
    uint16_t id;
    uint16_t gen;
    uint8_t  type;

    explicit Handle(uint16_t arg_id, const uint16_t arg_gen, const uint8_t arg_type) : id(arg_id), gen(arg_gen), type(arg_type) {
    }

    void set_gen(uint16_t arg_gen) {
        gen = arg_gen;
    }
};

namespace Detail {

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

struct Layout {
    Frame               frame{};
    std::vector<Handle> children{};
    float               spacing{};
    bool                vertical{false};
    Align               align{Align::Left};
};

enum class Kind : uint8_t { Rectangle = 0, Image = 1, Text = 2, Layout = 3 };

constexpr uint8_t k_clickable_bit = 1 << 5;
constexpr uint8_t k_invalid_type  = 0xff;

template <typename T> struct kind_of;

template <> struct kind_of<Rectangle> {
    static constexpr Kind value = Kind::Rectangle;
};

template <> struct kind_of<Image> {
    static constexpr Kind value = Kind::Image;
};

template <> struct kind_of<Text> {
    static constexpr Kind value = Kind::Text;
};

template <> struct kind_of<Layout> {
    static constexpr Kind value = Kind::Layout;
};

template <typename E>
inline constexpr uint8_t type_code_v =
    static_cast<uint8_t>(
        (static_cast<uint8_t>(kind_of<typename E::info_type>::value) << 6) |
        (E::clickable ? k_clickable_bit : 0));

template <typename T>
concept has_radius = requires(T t) { { t.radius } -> std::same_as<float&>; };

template <typename T>
concept has_color = requires(T t) { { t.color } -> std::same_as<Color&>; };

template <typename T>
concept is_element = requires {
    kind_of<typename T::info_type>::value;
};

template <typename T>
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
    using info_type = T;

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
            Log::type_name<T>()
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

class Clickable {
private:
    std::function<void()> lcb;
    std::function<void()> rcb;
};

template <typename T, typename... Features>
    requires is_element<T>
class Element : public T, public Features... {
private:
    bool dirty{true};

public:
    Element<T, Features...>() = default;

    bool click(float x, float y, bool left)
        requires (requires(Features f) { f.lcb; } && ...) {
        if (this->info.frame.width <= 0 || this->info.frame.height <= 0 ||
            x < this->info.frame.x || x > this->info.frame.x + this->info.frame.width ||
            y < this->info.frame.y || y > this->info.frame.y + this->info.frame.height) {
            return false;
        }
        const float corner_radius{clamp(this->radius, 0.0f, max(0.0f, min(this->info.frame.width, this->info.frame.height) / 2))};
        const float dx{x - clamp(x, this->info.frame.x + corner_radius, this->info.frame.x + this->info.frame.width - corner_radius)};
        const float dy{y - clamp(y, this->info.frame.y + corner_radius, this->info.frame.y + this->info.frame.height - corner_radius)};
        if (dx * dx + dy * dy > corner_radius * corner_radius) {
            return false;
        }

        if (left && this->lcb) {
            this->lcb();
            return true;
        }
        if (!left && this->rcb) {
            this->rcb();
            return true;
        }

        return false;
    }

    void set_dirty(bool d) { dirty = d; }

};

template <typename T>
class Pool {
private:
    struct Node {
        std::optional<T> t;
        uint16_t         gen{0};
    };

    std::deque<Node>      elements;
    std::vector<uint32_t> free;

public:
    template <typename... A>
    [[nodiscard]]
    std::pair<uint16_t, uint16_t> emplace(A&&... a) {
        uint16_t i;

        if (free.empty()) {
            i = static_cast<uint32_t>(elements.size());
            elements.emplace_back();
        }
        else {
            i = free.back();
            free.pop_back();
        }

        auto& node = elements[i];

        node.t.emplace(std::forward<A>(a)...);

        return {i, node.gen};
    }

    T* get(uint16_t i, uint16_t g) {
        if (i >= elements.size())
            return nullptr;

        auto& node = elements[i];

        if (node.gen != g || !node.t)
            return nullptr;

        return &*node.t;
    }

    void erase(uint16_t i, uint16_t g) {
        T* ptr = get(i, g);

        if (!ptr)
            return;

        elements[i].t.reset();
        ++elements[i].gen;

        free.push_back(i);
    }
};

template <typename T>
inline Pool<T> pool;

}

using Rectangle          = Detail::Element<Detail::Base<Detail::Rectangle> >;
using Image              = Detail::Element<Detail::Base<Detail::Image> >;
using Text               = Detail::Element<Detail::Base<Detail::Text> >;
using ClickableRectangle = Detail::Element<Detail::Base<Detail::Rectangle>, Detail::Clickable>;
using ClickableImage     = Detail::Element<Detail::Base<Detail::Image>, Detail::Clickable>;
using ClickableText      = Detail::Element<Detail::Base<Detail::Text>, Detail::Clickable>;

class Layout : public Detail::Base<Detail::Layout> {
public:
    // always add to the left
    void add_children(Handle id) { this->info.children.push_back(id); }

    void add_children(const std::vector<Handle>& ids) {
        for (auto id : ids) {
            this->info.children.push_back(id);
        }
    }

    void update();
};

// API

template <typename... Ts>
Handle add(Ts... ts) {
    Detail::Element elem{ts...};
    using Type = decltype(elem);

    auto [i,g] = Detail::pool<Type>.emplace(elem);

    return {i, g, elem.calculate_type()};
}

void remove(Handle id) {
    Detail::pool<Type>.erase(id);
}

}