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

/// @brief Index value meaning no node.
inline constexpr uint16_t k_none{0xFFFF};

/// @brief Generational handle to a node.
struct Handle {
	uint16_t id_{k_none};
	uint16_t gen_ = 0;

	/// @brief Returns true if the handles refer to the same node generation.
	constexpr bool operator==(const Handle&) const = default;

	/// @brief Returns true if the handle is non-empty.
	constexpr explicit operator bool() const { return id_ != k_none; }
};

/// @brief Pointer button used by input events.
enum class Button : uint8_t {
	Left,
	Right,
	Middle,
	None,
};

// Press -> ( DragStart, Drag*, DragEnd )            if the press became a drag
//       -> ( Release, [Click] )                     otherwise
// HoverEnter / HoverLeave are independent of the above.
/// @brief Kind of input event delivered to callbacks.
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

inline constexpr unsigned k_event_type_count{std::to_underlying(EventType::DragEnd) + 1};

// x / y are surface coordinates of the pointer when the event was generated.
// dx / dy: pointer delta for Drag, total offset from the press for DragStart, zero otherwise.
/// @brief Input event delivered to a bound callback.
struct Event {
	Handle    target_;
	uint32_t  tag_{};
	float     x_{}, y_{};
	float     dx_{}, dy_{};
	EventType type_{};
	Button    button_{Button::None};
};

// A set of EventTypes. Implicitly built from a single type, so `EventType::Click` and
// `EventType::HoverEnter | EventType::HoverLeave` are both valid masks.
/// @brief Bitset of EventType values.
class EventMask {
public:
	constexpr EventMask() = default;

	/// @brief Creates a mask containing only t.
	constexpr EventMask(EventType t) // NOLINT(google-explicit-constructor)
		:
		bits_(static_cast<uint8_t>(1u << std::to_underlying(t))) {}

	/// @brief Returns a mask containing every event type.
	[[nodiscard]] static constexpr EventMask all() { return from_bits((1u << k_event_type_count) - 1u); }

	/// @brief Returns true if the mask contains t.
	[[nodiscard]] constexpr bool has(EventType t) const { return (bits_ & EventMask{t}.bits_) != 0; }

	/// @brief Returns true if the mask shares any event type with m.
	[[nodiscard]] constexpr bool has_any(EventMask m) const { return (bits_ & m.bits_) != 0; }

	/// @brief Returns the mask with every event type in m removed.
	[[nodiscard]] constexpr EventMask without(EventMask m) const { return from_bits(bits_ & ~m.bits_); }

	/// @brief Returns true if the mask is non-empty.
	[[nodiscard]] constexpr explicit operator bool() const { return bits_ != 0; }

	/// @brief Returns the union of two masks.
	[[nodiscard]] friend constexpr EventMask operator|(EventMask a, EventMask b) {
		return from_bits(a.bits_ | b.bits_);
	}

	constexpr bool operator==(const EventMask&) const = default;

private:
	[[nodiscard]] static constexpr EventMask from_bits(unsigned bits) {
		EventMask m;
		m.bits_ = static_cast<uint8_t>(bits);
		return m;
	}

	uint8_t bits_ = 0;
};

static_assert(k_event_type_count <= 8, "EventMask stores one bit per EventType in a uint8_t");

/// @brief Combines two event types into a mask.
[[nodiscard]] constexpr EventMask operator|(const EventType a, const EventType b) {
	return EventMask{a} | b;
}

// Non-owning, trivially copyable callable: two pointers, never allocates.
//
//   EventCallback{on_click}                       free function `void(const Event&)`
//   EventCallback{[](const Event& e) { ... }}     capture-less lambda
//   EventCallback::bind<&View::on_click>(view)    member function, called on `view`
//   EventCallback::bind<&on_click>(state)         free function `void(State&, const Event&)`
//
// A bound object must outlive the binding (unbind it with Context::off, or destroy the node).
/// @brief Non-owning callable wrapper for input event callbacks.
class EventCallback {
public:
	using Fn = void (*)(const Event&);

	constexpr EventCallback() = default;

	/// @brief Wraps a free function or capture-less lambda.
	template<class F>
		requires std::is_convertible_v<F, Fn>
	EventCallback(F&& f) // NOLINT(google-explicit-constructor, bugprone-forwarding-reference-overload)
		:
		thunk_(&call_fn) {
		data_.fn_ = static_cast<Fn>(f);
		if(!data_.fn_)
			thunk_ = nullptr;
	}

	/// @brief Binds the member or free function F to obj.
	template<auto F, class T>
		requires std::is_invocable_v<decltype(F), T&, const Event&>
	[[nodiscard]] static EventCallback bind(T& obj) {
		EventCallback c;
		c.data_.obj_ = const_cast<void*>(static_cast<const void*>(std::addressof(obj)));
		c.thunk_     = [](Data d, const Event& e) { std::invoke(F, *static_cast<T*>(d.obj_), e); };
		return c;
	}

	/// @brief Returns true if the callback is bound.
	[[nodiscard]] constexpr explicit operator bool() const { return thunk_ != nullptr; }

	/// @brief Invokes the callback.
	void operator()(const Event& e) const { thunk_(data_, e); }

private:
	union Data {
		void* obj_;
		Fn    fn_;
	};

	using Thunk = void (*)(Data, const Event&);

	static void call_fn(Data d, const Event& e) { d.fn_(e); }

	Thunk thunk_{nullptr};
	Data  data_{.obj_ = nullptr};
};

static_assert(sizeof(EventCallback) == 2 * sizeof(void*));
static_assert(std::is_trivially_copyable_v<EventCallback>);

// One entry in a node's singly linked listener list. A (node, EventType) pair is bound to at
// most one listener, so dispatch is a short walk (nodes rarely carry more than two entries).
/// @brief One entry in a node's listener list.
struct Listener {
	EventCallback callback_{};
	uint16_t      next_{k_none};
	EventMask     mask_{};
};

/// @brief Returns the logical width and height of a texture.
[[nodiscard]] inline std::pair<float, float> texture_extent(const Texture& t) {
	return {t.get_logical_width(), t.get_logical_height()};
}

/// @brief Returns a mutable color channel by index.
[[nodiscard]] inline float& color_channel(Color& c, const unsigned i) {
	return c[i];
}

/// @brief Returns a color channel by index.
[[nodiscard]] inline float color_channel(const Color& c, const unsigned i) {
	return c[i];
}

/// @brief Kind of node content.
enum class Kind : uint8_t {
	Group,
	Rect,
	Image,
	Text,
	Ring,
};

/// @brief Layout axis.
enum class Axis : uint8_t {
	Horizontal = 0,
	Vertical   = 1,
};

// Fixed: size is whatever the user / animation set.
// Fit  : size comes from content (texture size, or children for a layout container).
// Grow : size is assigned by the parent's layout (equal share of the free space,
//        or stretched to the full cross axis).
// Layout never writes a Fixed axis, so animating Fixed sizes never fights layout.
/// @brief How a node derives its size along an axis.
enum class SizeMode : uint8_t {
	Fixed = 0,
	Fit   = 1,
	Grow  = 2,
};

/// @brief Main axis direction of a layout.
enum class Direction : uint8_t {
	Row,
	Column,
};

/// @brief Cross-axis alignment of layout children.
enum class Alignment : uint8_t {
	Start,
	Center,
	End,
};

/// @brief Main-axis distribution of layout children.
enum class Justify : uint8_t {
	Start,
	Center,
	End,
	SpaceBetween,
};

/// @brief Edge insets in pixels.
struct Insets {
	float left_{}, top_{}, right_{}, bottom_{};

	/// @brief Returns uniform insets on every edge.
	[[nodiscard]] static constexpr Insets all(float v) { return {v, v, v, v}; }

	/// @brief Returns horizontal and vertical insets.
	[[nodiscard]] static constexpr Insets xy(float x, float y) { return {x, y, x, y}; }
};

namespace Flag {

inline constexpr uint16_t k_visible{1u << 0u};
inline constexpr uint16_t k_dirty{1u << 1u};
inline constexpr uint16_t k_clip{1u << 2u};
inline constexpr uint16_t k_clickable{1u << 3u};
inline constexpr uint16_t k_hoverable{1u << 4u};
inline constexpr uint16_t k_draggable{1u << 5u};
inline constexpr uint16_t k_root{1u << 6u};

inline constexpr uint16_t k_interactive{k_clickable | k_hoverable | k_draggable};

inline constexpr unsigned k_size_shift_h{7};
inline constexpr unsigned k_size_shift_v{9};
inline constexpr uint16_t k_frozen{1u << 11u};

/// @brief Returns the bit shift that stores the size mode of an axis.
[[nodiscard]] constexpr unsigned size_shift(Axis a) {
	return a == Axis::Horizontal ? k_size_shift_h : k_size_shift_v;
}

}

/// @brief Extracts the size mode of an axis from a node's flags.
[[nodiscard]] constexpr SizeMode size_mode(uint16_t flags, Axis a) {
	return static_cast<SizeMode>((flags >> Flag::size_shift(a)) & 3u);
}

/// @brief Returns flags with the size mode of an axis replaced.
[[nodiscard]] constexpr uint16_t with_size_mode(uint16_t flags, Axis a, SizeMode m) {
	const unsigned s{Flag::size_shift(a)};
	return static_cast<uint16_t>((flags & ~(3u << s)) | std::to_underlying(m) << s);
}

/// @brief Returns the extent of a frame along an axis.
[[nodiscard]] constexpr float& extent(Frame& f, Axis a) {
	return a == Axis::Horizontal ? f.width_ : f.height_;
}

/// @brief Returns the extent of a frame along an axis.
[[nodiscard]] constexpr float extent(const Frame& f, Axis a) {
	return a == Axis::Horizontal ? f.width_ : f.height_;
}

/// @brief One node in the scene graph.
struct Node {
	Frame    frame_{};
	uint32_t tag_{};
	uint16_t parent_{k_none};
	uint16_t first_{k_none};
	uint16_t last_{k_none};
	uint16_t prev_{k_none};
	uint16_t next_{k_none};
	uint16_t gen_ = 0;
	uint16_t flags_{Flag::k_visible | Flag::k_dirty};
	uint16_t data_{k_none};
	uint16_t layout_{k_none};
	uint16_t listeners_{k_none}; // head of the Listener list
	uint16_t transform_{k_none};
	uint8_t  opacity_{255};
	Kind     kind_{Kind::Group};
};

static_assert(sizeof(Node) == 44, "Node is hot during layout / hit testing; keep it compact");

/// @brief Visual scaling transform of a node.
struct Transform {
	float scale_{1.f};
	float pivot_x_{.5f};
	float pivot_y_{.5f};
};

/// @brief A node frame placed in surface coordinates.
struct Placed {
	Frame frame_{};
	float scale_{1.f};
};

/// @brief Draw data of a rectangle node.
struct RectData {
	Color color_{};
	float radius_{};
};

// Arc starts at 12 o'clock and runs clockwise with round caps; `track` fills the rest.
/// @brief Draw data of a ring node.
struct RingData {
	Color color_{};
	Color track_{};
	float progress_{};
	float thickness_{};
};

/// @brief Draw data of an image node.
struct ImageData {
	Texture texture_{};
	float   radius_{};
	UvRect  uv_{};

	/// @brief Clears the draw data.
	void reset() {
		texture_.reset();
		radius_ = 0.f;
		uv_     = {};
	}
};

// Draws a coverage (R8) texture for the given output scale; `arg` is caller data.
using RasterFn = Texture (*)(float scale, float sub_x, float sub_y, uintptr_t arg);

/// @brief Draw data of a text node.
struct TextData {
	Texture     texture_{};
	FontHandle  font_{};
	Color       color_{};
	std::string text_{};
	RasterFn    raster_{}; // procedural glyph, redrawn on scale change like text
	uintptr_t   raster_arg_{};
	// Logical size from when the content was set; layout uses it so re-rasterizing at
	// another zoom (whose hinting changes the extent slightly) never re-lays out.
	float natural_w_{};
	float natural_h_{};
	float natural_origin_x_{};
	float natural_origin_y_{};
	// Identifies the pixels a raster at some scale would produce; equal keys never re-raster.
	uint64_t raster_key_{};

	/// @brief Sets the content texture and records its natural size.
	void set_content(Texture t) {
		texture_          = std::move(t);
		natural_w_        = texture_.is_valid() ? texture_.get_logical_width() : 0.f;
		natural_h_        = texture_.is_valid() ? texture_.get_logical_height() : 0.f;
		natural_origin_x_ = texture_.get_origin_x();
		natural_origin_y_ = texture_.get_origin_y();
	}

	/// @brief Clears the draw data.
	void reset() {
		texture_.reset();
		font_  = {};
		color_ = {};
		text_.clear();
		raster_           = nullptr;
		raster_arg_       = 0;
		natural_w_        = 0.f;
		natural_h_        = 0.f;
		natural_origin_x_ = 0.f;
		natural_origin_y_ = 0.f;
		raster_key_       = 0;
	}
};

/// @brief Configuration of a layout container.
struct LayoutData {
	Insets    padding_{};
	float     spacing_{};
	Direction dir_{Direction::Row};
	Alignment align_{Alignment::Start};
	Justify   justify_{Justify::Start};
};

/// @brief Weight and size limits for a growing child.
struct GrowParams {
	float weight_{1.f};
	float min_{0.f};
	float max_{std::numeric_limits<float>::infinity()};
};

/// @brief Grow parameters for both axes.
struct GrowData {
	GrowParams h_{};
	GrowParams v_{};
};

/// @brief Slot-based storage with stable indices and a free list.
template<class T>
class SlotVec {
public:
	/// @brief Stores value and returns its slot index, or k_none if full.
	[[nodiscard]] uint16_t acquire(T value) {
		if(!free_.empty()) {
			const uint16_t i{free_.back()};
			free_.pop_back();
			items_[i] = std::move(value);
			return i;
		}
		if(items_.size() >= k_none)
			return k_none;
		items_.push_back(std::move(value));
		return static_cast<uint16_t>(items_.size() - 1);
	}

	/// @brief Releases a slot for reuse.
	void release(uint16_t i) {
		if constexpr(requires(T& t) { t.reset(); })
			items_[i].reset();
		else
			items_[i] = T{};
		free_.push_back(i);
	}

	/// @brief Reserves storage for n slots.
	void reserve(size_t n) { items_.reserve(n); }

	/// @brief Returns the value stored in a slot.
	[[nodiscard]] T& operator[](uint16_t i) { return items_[i]; }

	/// @brief Returns the value stored in a slot.
	[[nodiscard]] const T& operator[](uint16_t i) const { return items_[i]; }

private:
	std::vector<T>        items_;
	std::vector<uint16_t> free_;
};

}
