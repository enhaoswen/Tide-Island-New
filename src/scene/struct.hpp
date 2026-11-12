#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
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

inline constexpr unsigned kEventTypeCount = static_cast<unsigned>(EventType::DragEnd) + 1;

// x / y are surface coordinates of the pointer when the event was generated.
// dx / dy: pointer delta for Drag, total offset from the press for DragStart, zero otherwise.
struct Event {
    Handle    target;
    uint32_t  tag{};
    float     x{},  y{};
    float     dx{}, dy{};
    EventType type{};
    Button    button{Button::None};
};

// A set of EventTypes. Implicitly built from a single type, so `EventType::Click` and
// `EventType::HoverEnter | EventType::HoverLeave` are both valid masks.
class EventMask {
public:
    constexpr EventMask() noexcept = default;

    constexpr EventMask(EventType t) noexcept // NOLINT(google-explicit-constructor)
        : bits_(static_cast<uint8_t>(1u << static_cast<unsigned>(t))) {
    }

    [[nodiscard]] static constexpr EventMask all() noexcept { return from_bits((1u << kEventTypeCount) - 1u); }

    [[nodiscard]] constexpr bool has(EventType t) const noexcept { return (bits_ & EventMask{t}.bits_) != 0; }
    [[nodiscard]] constexpr bool any(EventMask m) const noexcept { return (bits_ & m.bits_) != 0; }

    [[nodiscard]] constexpr EventMask without(EventMask m) const noexcept { return from_bits(bits_ & ~m.bits_); }

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return bits_ != 0; }

    [[nodiscard]] friend constexpr EventMask operator|(EventMask a, EventMask b) noexcept {
        return from_bits(a.bits_ | b.bits_);
    }

    constexpr bool operator==(const EventMask&) const = default;

private:
    [[nodiscard]] static constexpr EventMask from_bits(unsigned bits) noexcept {
        EventMask m;
        m.bits_ = static_cast<uint8_t>(bits);
        return m;
    }

    uint8_t bits_ = 0;
};

static_assert(kEventTypeCount <= 8, "EventMask stores one bit per EventType in a uint8_t");

[[nodiscard]] constexpr EventMask operator|(EventType a, EventType b) noexcept { return EventMask{a} | b; }

// Non-owning, trivially copyable callable: two pointers, never allocates.
//
//   EventCallback{on_click}                       free function `void(const Event&)`
//   EventCallback{[](const Event& e) { ... }}     capture-less lambda
//   EventCallback::bind<&View::on_click>(view)    member function, called on `view`
//   EventCallback::bind<&on_click>(state)         free function `void(State&, const Event&)`
//
// A bound object must outlive the binding (unbind it with Context::off, or destroy the node).
class EventCallback {
public:
    using Fn = void (*)(const Event&);

    constexpr EventCallback() noexcept = default;

    template <class F>
        requires std::is_convertible_v<F, Fn>
    EventCallback(F&& f) noexcept // NOLINT(google-explicit-constructor, bugprone-forwarding-reference-overload)
        : thunk_(&call_fn) {
        data_.fn = static_cast<Fn>(f);
        if (!data_.fn)
            thunk_ = nullptr;
    }

    template <auto F, class T>
        requires std::is_invocable_v<decltype(F), T&, const Event&>
    [[nodiscard]] static EventCallback bind(T& obj) noexcept {
        EventCallback c;
        c.data_.obj = const_cast<void*>(static_cast<const void*>(std::addressof(obj)));
        c.thunk_    = [](Data d, const Event& e) { std::invoke(F, *static_cast<T*>(d.obj), e); };
        return c;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return thunk_ != nullptr; }

    void operator()(const Event& e) const { thunk_(data_, e); }

private:
    union Data {
        void* obj;
        Fn    fn;
    };

    using Thunk = void (*)(Data, const Event&);

    static void call_fn(Data d, const Event& e) { d.fn(e); }

    Thunk thunk_ = nullptr;
    Data  data_{.obj = nullptr};
};

static_assert(sizeof(EventCallback) == 2 * sizeof(void*));
static_assert(std::is_trivially_copyable_v<EventCallback>);

// One entry in a node's singly linked listener list. A (node, EventType) pair is bound to at
// most one listener, so dispatch is a short walk (nodes rarely carry more than two entries).
struct Listener {
    EventCallback callback{};
    uint16_t      next = kNone;
    EventMask     mask{};
};


[[nodiscard]] inline std::pair<float, float> texture_extent(const Texture& t) noexcept {
    return {t.logical_width(), t.logical_height()};
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
    uint16_t layout    = kNone;
    uint16_t listeners = kNone; // head of the Listener list
    uint8_t  opacity   = 255;
    Kind     kind      = Kind::Group;
};

static_assert(sizeof(Node) == 44, "Node is hot during layout / hit testing; keep it compact");

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
    Texture     texture{};
    FontHandle  font{};
    Color       color{};
    std::string text{};

    void reset() {
        texture.reset();
        font  = {};
        color = {};
        text.clear();
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