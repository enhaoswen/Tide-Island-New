#pragma once

#include "graphics/texture.hpp"
#include "utils/color.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <source_location>
#include <utility>
#include <vector>

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

template <typename T>
class Ref;

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

// type layout: aabc 0000
// aa = kind, b = clickable, c = layouter
enum class Kind : uint8_t { Rectangle = 0, Image = 1, Text = 2 };

constexpr uint8_t k_clickable_bit = 1 << 5;
constexpr uint8_t k_layouter_bit  = 1 << 4;

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

template <typename T>
inline constexpr uint8_t type_code_v =
    static_cast<uint8_t>(
        (static_cast<uint8_t>(kind_of<typename T::info_type>::value) << 6) |
        (T::clickable ? k_clickable_bit : 0) |
        (T::layouter ? k_layouter_bit : 0));

template <typename T>
concept has_radius = requires(T t) { { t.radius } -> std::same_as<float&>; };

template <typename T>
concept has_color = requires(T t) { { t.color } -> std::same_as<Color&>; };

template <typename T>
concept has_fix = requires(T t) { { t.fix } -> std::same_as<bool&>; };

template <typename T>
concept is_element = requires { kind_of<typename T::info_type>::value; };

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

    Base() = default;

    explicit Base(T arg_desc) : info(std::move(arg_desc)) {
    }

    std::pair<float, float> actual_size()
        requires has_fix<T> {
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
            Log::type_name<T>()
            );
    }

    [[nodiscard]] float left() { return info.frame.x; }
    [[nodiscard]] float right() { return info.frame.x + info.frame.width; }
    [[nodiscard]] float top() { return info.frame.y; }
    [[nodiscard]] float bottom() { return info.frame.y + info.frame.height; }

};

struct Clickable {
    std::function<void()> lcb;
    std::function<void()> rcb;
};

class Layouter {
private:
    std::vector<Handle> children;

public:
    void add_children(Handle id) { children.push_back(id); }

    void add_children(const std::vector<Handle>& ids) {
        for (auto id : ids) {
            children.push_back(id);
        }
    }

};

template <typename T, typename... Features>
    requires is_element<T>
class Element : public T, public Features... {
private:
    bool dirty_{true};

public:
    static constexpr bool clickable = (std::same_as<Features, Clickable> || ...);
    static constexpr bool layouter  = (std::same_as<Features, Layouter> || ...);

    using T::T;

    Element() = default;

    bool click(float x, float y, bool left)
        requires clickable {
        if (this->info.frame.width <= 0 || this->info.frame.height <= 0 ||
            x < this->info.frame.x || x > this->info.frame.x + this->info.frame.width ||
            y < this->info.frame.y || y > this->info.frame.y + this->info.frame.height) {
            return false;
        }

        float corner_radius{};
        if constexpr (has_radius<typename T::info_type>) {
            corner_radius = std::clamp(this->info.radius, 0.0f, std::max(0.0f, std::min(this->info.frame.width, this->info.frame.height) / 2));
        }
        const float dx{x - std::clamp(x, this->info.frame.x + corner_radius, this->info.frame.x + this->info.frame.width - corner_radius)};
        const float dy{y - std::clamp(y, this->info.frame.y + corner_radius, this->info.frame.y + this->info.frame.height - corner_radius)};
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

    [[nodiscard]] bool dirty() const { return dirty_; }
    void               set_dirty(bool d) { dirty_ = d; }

    void update()
        requires layouter {


    }

};

template <typename T>
class Pool {
private:
    struct Node {
        std::optional<T> t;
        uint16_t         gen{0};
    };

    std::deque<Node>      elements;
    std::vector<uint16_t> free;

public:
    template <typename... A>
    [[nodiscard]]
    std::pair<uint16_t, uint16_t> emplace(A&&... a) {
        uint16_t i;

        if (free.empty()) {
            i = static_cast<uint16_t>(elements.size());
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
        if (!get(i, g))
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
using Layout             = Detail::Element<Detail::Base<Detail::Rectangle>, Detail::Layouter>;
using ClickableRectangle = Detail::Element<Detail::Base<Detail::Rectangle>, Detail::Clickable>;
using ClickableImage     = Detail::Element<Detail::Base<Detail::Image>, Detail::Clickable>;
using ClickableText      = Detail::Element<Detail::Base<Detail::Text>, Detail::Clickable>;
using ClickableLayout    = Detail::Element<Detail::Base<Detail::Rectangle>, Detail::Clickable, Detail::Layouter>;

// some magic here
namespace Detail {

using remove_cb = void (*)(uint16_t, uint16_t);

template <typename... Es>
struct TypeList {
};

using AllElements = TypeList<
    Object::Rectangle, Object::Image, Object::Text, Object::Layout,
    Object::ClickableRectangle, Object::ClickableImage, Object::ClickableText, Object::ClickableLayout
>;

template <typename... Es>
constexpr bool all_unique(TypeList<Es...>) {
    constexpr std::array<uint8_t, sizeof...(Es)> codes{type_code_v<Es>...};
    for (std::size_t i = 0; i < codes.size(); ++i)
        for (std::size_t j = i + 1; j < codes.size(); ++j)
            if (codes[i] == codes[j])
                return false;
    return true;
}

static_assert(all_unique(AllElements{}), "duplicate Handle type code");

template <typename T>
T* get(const Handle& h) {
    if (h.type != Detail::type_code_v<T>)
        return nullptr;
    return Detail::pool<T>.get(h.id, h.gen);
}

template <typename T>
class Ref : public Handle {
public:
    using Handle::Handle;

    T* get() const { return pool<T>.get(id, gen); }
    T* operator->() const { return get(); }
    explicit operator bool() const { return get() != nullptr; }
};

template <typename T>
void erase_impl(uint16_t i, uint16_t g) {
    pool<T>.erase(i, g);
}

template <typename... Es>
constexpr std::array<remove_cb, 256> make_erase_table(TypeList<Es...>) {
    std::array<remove_cb, 256> t{};
    ((t[type_code_v<Es>] = &erase_impl<Es>), ...);
    return t;
}

inline constexpr auto erase_table = make_erase_table(AllElements{});

}

// API

template <typename T, typename... Args>
Ref<T> add(Args&&... args) {
    auto [i, g] = Detail::pool<T>.emplace(std::forward<Args>(args)...);
    return {i, g, Detail::type_code_v<T>};
}

inline void remove(Handle h) {
    if (const auto fn = Detail::erase_table[h.type])
        fn(h.id, h.gen);
    else
        Log::logger(Log::Warning, "remove: invalid handle type {:#04x}", h.type);
}

}