#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "graphics/texture.hpp"
#include "utils/color.hpp"
#include "utils/struct.hpp"

namespace Object {

// Index value meaning no node
inline constexpr uint16_t kNone = 0xFFFF;

struct Handle {
    uint16_t id  = kNone;
    uint16_t gen = 0;

    constexpr bool operator==(const Handle&) const = default;

    constexpr explicit operator bool() const noexcept { return id != kNone; }
};

enum struct Button : uint8_t { Left, Right, Middle, None };

// Press -> ( DragStart, Drag*, DragEnd )            if the press became a drag
//       -> ( Release, [Click] )                     otherwise
// HoverEnter / HoverLeave are independent of the above.
enum class EventType : uint8_t {
    Press,
    Release,
    Click,
    HoverEnter,
    HoverLeave,
    DragStart,
    Drag,
    DragEnd,
};

struct Event {
    Handle    target;
    uint32_t  tag{};
    float     x{},  y{};
    float     dx{}, dy{};
    EventType type{};
    Button    button{Button::None};
};


[[nodiscard]] inline std::pair<float, float> texture_extent(const Texture& t) noexcept {
    return {static_cast<float>(t.width()), static_cast<float>(t.height())};
}

[[nodiscard]] inline float& color_channel(Color& c, const unsigned i) noexcept { return c[i]; }
[[nodiscard]] inline float  color_channel(const Color& c, const unsigned i) noexcept { return c[i]; }

// ============================================================================

enum class Kind : uint8_t { Group, Rect, Image, Text };

enum class Axis : uint8_t { Horizontal = 0, Vertical = 1 };

// Fixed: size is whatever the user / animation set.
// Fit  : size comes from content (texture size, or children for a layout container).
// Grow : size is assigned by the parent's layout (equal share of the free space,
//        or stretched to the full cross axis).
// Layout never writes a Fixed axis, so animating Fixed sizes never fights layout.
enum class SizeMode : uint8_t { Fixed = 0, Fit = 1, Grow = 2 };

enum class Direction : uint8_t { Row, Column };

enum class Alignment : uint8_t { Start, Center, End };             // cross axis
enum class Justify : uint8_t { Start, Center, End, SpaceBetween }; // main axis

struct Insets {
    float left{}, top{}, right{}, bottom{};

    [[nodiscard]] static constexpr Insets all(float v) noexcept { return {v, v, v, v}; }
    [[nodiscard]] static constexpr Insets xy(float x, float y) noexcept { return {x, y, x, y}; }
};

namespace flag {

inline constexpr uint16_t visible   = 1u << 0;
inline constexpr uint16_t dirty     = 1u << 1;
inline constexpr uint16_t clip      = 1u << 2;
inline constexpr uint16_t clickable = 1u << 3;
inline constexpr uint16_t hoverable = 1u << 4;
inline constexpr uint16_t draggable = 1u << 5;
inline constexpr uint16_t root      = 1u << 6;

inline constexpr uint16_t interactive = clickable | hoverable | draggable;

inline constexpr unsigned size_shift_h = 7;
inline constexpr unsigned size_shift_v = 9;
inline constexpr uint16_t frozen       = 1u << 11;

[[nodiscard]] constexpr unsigned size_shift(Axis a) noexcept {
    return a == Axis::Horizontal ? size_shift_h : size_shift_v;
}

}

[[nodiscard]] constexpr SizeMode size_mode(uint16_t flags, Axis a) noexcept {
    return static_cast<SizeMode>((flags >> flag::size_shift(a)) & 3u);
}

[[nodiscard]] constexpr uint16_t with_size_mode(uint16_t flags, Axis a, SizeMode m) noexcept {
    const unsigned s = flag::size_shift(a);
    return static_cast<uint16_t>((flags & ~(3u << s)) | (static_cast<unsigned>(m) << s));
}

[[nodiscard]] constexpr float& extent(Frame& f, Axis a) noexcept {
    return a == Axis::Horizontal ? f.width : f.height;
}

[[nodiscard]] constexpr float extent(const Frame& f, Axis a) noexcept {
    return a == Axis::Horizontal ? f.width : f.height;
}

struct Node {
    Frame    frame{};
    uint32_t tag{};
    uint16_t parent  = kNone;
    uint16_t first   = kNone;
    uint16_t last    = kNone;
    uint16_t prev    = kNone;
    uint16_t next    = kNone;
    uint16_t gen     = 0;
    uint16_t flags   = flag::visible | flag::dirty;
    uint16_t data    = kNone;
    uint16_t layout  = kNone;
    uint8_t  opacity = 255;
    Kind     kind    = Kind::Group;
};

struct RectData {
    Color color{};
    float radius{};
};

struct ImageData {
    Texture texture{};
    float   radius{};

    void reset() {
        texture.reset();
        radius = 0.f;
    }
};

struct TextData {
    Texture    texture{};
    FontHandle font{};
    Color      color{};

    void reset() {
        texture.reset();
        font  = {};
        color = {};
    }
};

struct LayoutData {
    Insets    padding{};
    float     spacing{};
    Direction dir     = Direction::Row;
    Alignment align   = Alignment::Start;
    Justify   justify = Justify::Start;
};

struct GrowParams {
    float weight = 1.f;
    float min    = 0.f;
    float max    = std::numeric_limits<float>::infinity();
};

struct GrowData {
    GrowParams h{};
    GrowParams v{};
};

template <class T>
class SlotVec {
public:
    [[nodiscard]] uint16_t acquire(T value) {
        if (!free_.empty()) {
            const uint16_t i = free_.back();
            free_.pop_back();
            items_[i] = std::move(value);
            return i;
        }
        if (items_.size() >= kNone)
            return kNone;
        items_.push_back(std::move(value));
        return static_cast<uint16_t>(items_.size() - 1);
    }

    void release(uint16_t i) {
        if constexpr (requires(T& t) { t.reset(); })
            items_[i].reset();
        else
            items_[i] = T{};
        free_.push_back(i);
    }

    void reserve(std::size_t n) { items_.reserve(n); }

    [[nodiscard]] T&       operator[](uint16_t i) noexcept { return items_[i]; }
    [[nodiscard]] const T& operator[](uint16_t i) const noexcept { return items_[i]; }

private:
    std::vector<T>        items_;
    std::vector<uint16_t> free_;
};

}