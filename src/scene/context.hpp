#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "animation.hpp"
#include "struct.hpp"

namespace Object {

/// @brief Description of a rectangle to create.
struct RectDesc {
	Frame frame_{};
	Color color_{};
	float radius_{};
};

/// @brief Description of an image to create.
struct ImageDesc {
	Frame   frame_{};
	Texture texture_{};
	float   radius_{};
};

/// @brief Description of a text node to create.
struct TextDesc {
	Frame      frame_{};
	Texture    texture_{};
	FontHandle font_{};
	Color      color_{};
};

/// @brief Description of a ring to create.
struct RingDesc {
	Frame                frame_{};
	Color                color_{};
	std::optional<Color> track_{};
	float                progress_{};
	float                thickness_{4.f};
};

/// @brief Description of a spring animation.
struct SAnimDesc {
	Handle        h_;
	float         to_{};
	const Spring& s_;
	float         delay_{};
	Prop          p_{};
	AnimCallback  on_done_{};
};

/// @brief Description of a timed animation.
struct TAnimDesc {
	Handle        h_;
	float         to_{};
	const Timing& t_;
	float         delay_{};
	Prop          p_{};
	AnimCallback  on_done_{};
};

class Context;

/// @brief Typed accessor for a rectangle node.
class RectView {
public:
	RectView() = default;

	/// @brief Returns true if the view is valid.
	[[nodiscard]] explicit operator bool() const;

	/// @brief Sets the rectangle fill color.
	RectView& set_color(const Color& c);

	/// @brief Sets the corner radius in pixels.
	RectView& set_radius(float r);

	/// @brief Returns the current color.
	[[nodiscard]] Color get_color() const;

	/// @brief Returns the corner radius in pixels.
	[[nodiscard]] float get_radius() const;

private:
	friend class Context;

	RectView(Context* c, Handle h) :
		ctx_(c),
		h_(h) {}

	[[nodiscard]] RectData* data() const;

	Context* ctx_{nullptr};
	Handle   h_{};
};

/// @brief Typed accessor for an image node.
class ImageView {
public:
	ImageView() = default;

	/// @brief Returns true if the view is valid.
	[[nodiscard]] explicit operator bool() const;

	/// @brief Replaces the image texture and re-lays out.
	ImageView& set_texture(Texture t); // may change the Fit size -> re-layout
	/// @brief Loads an image from path.
	ImageView& set_image(std::string_view path); // loads then set_texture; no path stored
	/// @brief Sets the corner radius in pixels.
	ImageView& set_radius(float r);
	/// @brief Sets the source rectangle of the texture.
	ImageView& set_uv(const UvRect& uv); // e.g. one frame of a sprite strip

	/// @brief Returns the corner radius in pixels.
	[[nodiscard]] float get_radius() const;

private:
	friend class Context;

	ImageView(Context* c, Handle h) :
		ctx_(c),
		h_(h) {}

	[[nodiscard]] ImageData* data() const;

	Context* ctx_{nullptr};
	Handle   h_{};
};

/// @brief Typed accessor for a ring node.
class RingView {
public:
	RingView() = default;

	/// @brief Returns true if the view is valid.
	[[nodiscard]] explicit operator bool() const;

	/// @brief Sets the progress in [0, 1].
	RingView& set_progress(float p); // clamped to [0, 1]
	/// @brief Sets the ring thickness in pixels.
	RingView& set_thickness(float t);

	/// @brief Sets the ring color.
	RingView& set_color(const Color& c);

	/// @brief Sets the track color.
	RingView& set_track(const Color& c);

	/// @brief Returns the progress in [0, 1].
	[[nodiscard]] float get_progress() const;

	/// @brief Returns the ring thickness in pixels.
	[[nodiscard]] float get_thickness() const;

	/// @brief Returns the current color.
	[[nodiscard]] Color get_color() const;

	/// @brief Returns the track color.
	[[nodiscard]] Color get_track() const;

private:
	friend class Context;

	RingView(Context* c, Handle h) :
		ctx_(c),
		h_(h) {}

	[[nodiscard]] RingData* data() const;

	Context* ctx_{nullptr};
	Handle   h_{};
};

/// @brief Typed accessor for a text node.
class TextView {
public:
	TextView() = default;

	/// @brief Returns true if the view is valid.
	[[nodiscard]] explicit operator bool() const;

	/// @brief Sets a pre-rendered text texture.
	TextView& set_texture(Texture t); // text is rendered to a texture by the caller
	// Procedural glyph: `fn` draws it now and again whenever the output scale changes.
	/// @brief Sets a procedural glyph renderer.
	TextView& set_raster(RasterFn fn, uintptr_t arg);

	// Logical size of the current texture (0 x 0 if none).
	/// @brief Returns the logical texture width.
	[[nodiscard]] float get_texture_width() const;

	/// @brief Returns the logical texture height.
	[[nodiscard]] float get_texture_height() const;

	/// @brief Sets the text and rasterizes it with the stored font.
	TextView& set_text(std::string_view s); // rasterizes with the stored font; keeps the string for scale changes
	/// @brief Sets the font used for rasterization.
	TextView& set_font(FontHandle f);

	/// @brief Sets the text color.
	TextView& set_color(const Color& c);

	/// @brief Returns the current color.
	[[nodiscard]] Color get_color() const;

private:
	friend class Context;

	TextView(Context* c, Handle h) :
		ctx_(c),
		h_(h) {}

	[[nodiscard]] TextData* data() const;

	Context* ctx_{nullptr};
	Handle   h_{};
};

/// @brief Scene graph owning nodes, draw data, layout and input state.
class Context {
public:
	/// @brief Creates an empty context, reserving room for reserve_nodes nodes.
	explicit Context(size_t reserve_nodes = 128);

	/// @brief Contexts are non-copyable; node indices would alias.
	Context(const Context&) = delete;

	/// @brief Contexts are non-copyable; node indices would alias.
	Context& operator=(const Context&) = delete;

	/// @brief Moves the whole scene graph.
	Context(Context&&) = default;

	/// @brief Move-assigns the whole scene graph.
	Context& operator=(Context&&) = default;

	/// @brief Creates a root node covering the given surface size.
	Handle create_root(float width, float height);

	/// @brief Creates a group node.
	Handle create_group(Handle parent = {}, const Frame& frame = {});

	/// @brief Creates a rectangle node.
	Handle create_rect(Handle parent = {}, const RectDesc& desc = {});

	/// @brief Creates an image node.
	Handle create_image(Handle parent = {}, ImageDesc desc = {});

	/// @brief Creates a text node.
	Handle create_text(Handle parent = {}, TextDesc desc = {});

	/// @brief Creates a ring node.
	Handle create_ring(Handle parent = {}, const RingDesc& desc = {});

	/// @brief Destroys a node and its whole subtree.
	void destroy(Handle h); // whole subtree; stale handles become invalid
	/// @brief Destroys every root node.
	void clear(); // destroy every root

	/// @brief Returns true if the handle refers to a live node.
	[[nodiscard]] bool is_alive(Handle h) const { return node(h) != nullptr; }

	/// @brief Returns the number of live nodes.
	[[nodiscard]] size_t get_node_count() const { return live_; }

	/// @brief Attaches a child on top of parent; returns true on success.
	bool attach(Handle child, Handle parent); // appended on top; re-parents if needed
	/// @brief Detaches a node from its parent; returns true on success.
	bool detach(Handle h);

	/// @brief Moves a node to the front of its siblings.
	void move_to_front(Handle h); // z-order
	/// @brief Moves a node to the back of its siblings.
	void move_to_back(Handle h);

	/// @brief Returns the parent handle, or an empty handle.
	[[nodiscard]] Handle parent(Handle h) const;

	/// @brief Range over the children of a node.
	class ChildRange {
	public:
		/// @brief Forward iterator over the children of a node.
		class iterator {
		public:
			/// @brief Returns the handle of the current child.
			Handle operator*() const { return ctx_->handle_of(i_); }

			/// @brief Advances to the next sibling.
			iterator& operator++() {
				i_ = ctx_->nodes_[i_].next_;
				return *this;
			}

			/// @brief Returns true if both iterators point at the same child.
			bool operator==(const iterator&) const = default;

		private:
			friend class ChildRange;

			iterator(const Context* c, uint16_t i) :
				ctx_(c),
				i_(i) {}

			const Context* ctx_;
			uint16_t       i_;
		};

		/// @brief Returns an iterator to the first child.
		[[nodiscard]] iterator begin() const { return {ctx_, first_}; }

		/// @brief Returns the end iterator.
		[[nodiscard]] iterator end() const { return {ctx_, k_none}; }

	private:
		friend class Context;

		ChildRange(const Context* c, uint16_t f) :
			ctx_(c),
			first_(f) {}

		const Context* ctx_;
		uint16_t       first_;
	};

	// Back-most to front-most. Do not create / destroy / re-parent while iterating.
	/// @brief Returns the children of a node, back to front.
	[[nodiscard]] ChildRange children(Handle h) const;

	/// @brief Sets the node position.
	void set_pos(Handle h, float x, float y);

	/// @brief Sets the node size.
	void set_size(Handle h, float w, float hgt);

	/// @brief Sets how the node derives its size along an axis.
	void set_size_mode(Handle h, Axis axis, SizeMode mode);

	/// @brief Sets grow weight and limits along an axis.
	void set_grow(Handle h, Axis axis, const GrowParams& params = {});

	/// @brief Shows or hides the node.
	void set_visible(Handle h, bool is_visible);

	/// @brief Sets the node opacity in [0, 1].
	void set_opacity(Handle h, float v);

	// Visual only: layout ignores it; drawing and hit testing follow it.
	/// @brief Sets the visual scale of the node.
	void set_scale(Handle h, float s);

	// Normalized point of the node that stays fixed while scaling; (0.5, 0.5) by default.
	/// @brief Sets the normalized pivot used while scaling.
	void set_pivot(Handle h, float x, float y);

	/// @brief Returns the visual scale of the node.
	[[nodiscard]] float scale(Handle h) const;

	/// @brief Enables or disables clipping to the node bounds.
	void set_clip(Handle h, bool is_enabled);

	/// @brief Sets the caller tag of the node.
	void set_tag(Handle h, uint32_t tag);

	/// @brief Enables or disables click handling.
	void set_clickable(Handle h, bool is_enabled);

	/// @brief Enables or disables hover handling.
	void set_hoverable(Handle h, bool is_enabled);

	/// @brief Enables or disables drag handling.
	void set_draggable(Handle h, bool is_enabled);

	/// @brief Sets the layout configuration of a node.
	void set_layout(Handle h, const LayoutData& layout);

	/// @brief Removes the layout configuration of a node.
	void clear_layout(Handle h);

	/// @brief Returns the local frame of the node.
	[[nodiscard]] Frame frame(Handle h) const;

	/// @brief Returns the frame after applying parent scaling.
	[[nodiscard]] Frame absolute_frame(Handle h) const; // after scaling

	/// @brief Returns the caller tag of the node.
	[[nodiscard]] uint32_t tag(Handle h) const;

	/// @brief Reads an animatable property, if supported.
	[[nodiscard]] std::optional<float> get_prop(Handle h, Prop p) const;

	/// @brief Writes an animatable property; returns true if supported.
	bool set_prop(Handle h, Prop p, float value);

	// Typed views; empty (false) if the handle is dead or of another Kind.
	/// @brief Returns a typed view of a rectangle node.
	[[nodiscard]] RectView rect(Handle h);

	/// @brief Returns a typed view of an image node.
	[[nodiscard]] ImageView image(Handle h);

	/// @brief Returns a typed view of a text node.
	[[nodiscard]] TextView text(Handle h);

	/// @brief Returns a typed view of a ring node.
	[[nodiscard]] RingView ring(Handle h);

	/// @brief Starts a spring animation.
	bool animate(SAnimDesc desc);

	/// @brief Starts a timed animation.
	bool animate(TAnimDesc desc);

	/// @brief Stops the animation of a property.
	void cancel_animation(Handle h, Prop p) { anim_.cancel(h, p); }

	/// @brief Returns true if the property is animated.
	[[nodiscard]] bool is_animating(Handle h, Prop p) const { return anim_.is_active(h, p); }

	/// @brief Returns true if any animation is running.
	[[nodiscard]] bool is_animating() const { return !anim_.is_empty(); }

	/// @brief Advances animations and layout by dt milliseconds.
	void update(float dt);

	/// @brief Lays out dirty roots; returns true if any work was done.
	bool layout();

	// Events go to the top-most interactive node under the pointer only (no bubbling).
	// Each (node, EventType) pair holds at most one callback: binding replaces what was bound
	// to those types, and an empty callback unbinds them. Binding also turns on the interaction
	// flag the types need (Click -> clickable, Hover* -> hoverable, Drag* -> draggable,
	// Press / Release -> clickable unless already clickable or draggable). Unbinding leaves
	// flags alone, so a node made clickable keeps blocking clicks to whatever is beneath it.
	// Bindings die with the node.
	/// @brief Binds a callback for the given event types.
	bool on(Handle h, EventMask mask, EventCallback cb);

	/// @brief Binds a click callback.
	bool on_click(Handle h, EventCallback cb) { return on(h, EventType::Click, cb); }

	/// @brief Unbinds callbacks for the given event types.
	void off(Handle h, EventMask mask = EventMask::all());

	// Runs the callbacks for queued events, in order. Call it outside input handling; callbacks
	// may freely create, destroy, rebind or animate nodes (including their own target).
	// Returns whether any callback ran.
	/// @brief Dispatches queued events; returns true if any callback ran.
	bool dispatch_events();

	// Only events that have a bound callback are queued, so unhandled input costs nothing.
	/// @brief Updates the pointer position.
	void pointer_move(float x, float y);

	/// @brief Handles a pointer button press or release.
	void pointer_button(Button b, bool is_down);

	/// @brief Cancels the current pointer interaction.
	void pointer_cancel(); // pointer left the surface, focus lost etc.
	/// @brief Returns the top-most interactive node at a point.
	[[nodiscard]] Handle hit_test(float x, float y) const; // top-most interactive node

	/// @brief Returns the currently hovered node.
	[[nodiscard]] Handle get_hovered() const { return hovered_; }

	/// @brief Sets the distance a pointer must move to start a drag.
	void set_drag_threshold(float px) { drag_threshold_ = px; }

private:
	friend class RectView;
	friend class ImageView;
	friend class TextView;
	friend class RingView;
	friend struct Drawer;

	[[nodiscard]] Node* node(Handle h) {
		return h.id_ < nodes_.size() && nodes_[h.id_].gen_ == h.gen_ ? &nodes_[h.id_] : nullptr;
	}

	[[nodiscard]] const Node* node(Handle h) const {
		return h.id_ < nodes_.size() && nodes_[h.id_].gen_ == h.gen_ ? &nodes_[h.id_] : nullptr;
	}

	[[nodiscard]] Handle handle_of(uint16_t i) const { return {i, nodes_[i].gen_}; }

	[[nodiscard]] uint16_t alloc_node();

	void release_node(uint16_t i);

	void release_data(Kind kind, uint16_t data);

	[[nodiscard]] Handle spawn(Kind kind, uint16_t data, const Frame& frame, Handle parent);

	void link_last(uint16_t parent, uint16_t child);

	void link_first(uint16_t parent, uint16_t child);

	uint16_t unlink(uint16_t child);

	void mark_dirty(uint16_t i);

	[[nodiscard]] float text_zoom(uint16_t i) const;

	[[nodiscard]] static uint64_t raster_key(const TextData& d, float scale, int phase_x, int phase_y);

	[[nodiscard]] static Texture raster_text(const TextData& d, float scale, float sub_x, float sub_y);

	void rasterize(uint16_t i, TextData& d);

	[[nodiscard]] static Frame prepare_text_draw(TextData& d, const Frame& abs);

	void set_flag(Handle h, uint16_t bit, bool is_on);

	void measure(uint16_t i);

	void arrange(uint16_t i);

	void arrange_children(Node& n);

	float resolve_grow(Node& n, Axis axis, float space);

	[[nodiscard]] GrowParams grow_params(uint16_t id, Axis axis) const;

	[[nodiscard]] float radius_of(const Node& n) const;

	[[nodiscard]] Transform& transform_of(Node& n);

	[[nodiscard]] Placed place(const Node& n, const Placed& parent) const;

	[[nodiscard]] Placed placed(uint16_t i) const;

	[[nodiscard]] uint16_t hit_node(uint16_t i, float x, float y, const Placed& parent) const;

	void update_hover();

	void validate_input();

	void emit(Handle target, EventType type, Button b, float dx, float dy);

	[[nodiscard]] bool poll_event(Event& out);

	[[nodiscard]] EventCallback listener(const Node& n, EventType type) const;

	void unbind(Node& n, EventMask mask);

	std::vector<Node>   nodes_;
	uint16_t            free_head_{k_none};
	size_t              live_ = 0;
	std::vector<Handle> roots_; // back-to-front

	SlotVec<RectData>                      rects_;
	SlotVec<ImageData>                     images_;
	SlotVec<TextData>                      texts_;
	SlotVec<RingData>                      rings_;
	SlotVec<LayoutData>                    layouts_;
	SlotVec<Listener>                      listeners_;
	SlotVec<Transform>                     transforms_;
	std::unordered_map<uint16_t, GrowData> grow_;

	Animator anim_;

	std::vector<Event> events_;
	size_t             event_head_ = 0;
	Handle             hovered_{};
	Handle             pressed_{};
	Button             pressed_button_{Button::None};
	float              px_{}, py_{};
	float              press_x_{}, press_y_{};
	float              drag_threshold_{4.f};
	bool               has_pointer_{false};
	bool               is_dragging_{false};
};

inline RectData* RectView::data() const {
	Node* n{ctx_ ? ctx_->node(h_) : nullptr};
	return n ? &ctx_->rects_[n->data_] : nullptr;
}

inline ImageData* ImageView::data() const {
	Node* n{ctx_ ? ctx_->node(h_) : nullptr};
	return n ? &ctx_->images_[n->data_] : nullptr;
}

inline TextData* TextView::data() const {
	Node* n{ctx_ ? ctx_->node(h_) : nullptr};
	return n ? &ctx_->texts_[n->data_] : nullptr;
}

inline RingData* RingView::data() const {
	Node* n{ctx_ ? ctx_->node(h_) : nullptr};
	return n ? &ctx_->rings_[n->data_] : nullptr;
}

inline Placed Context::place(const Node& n, const Placed& parent) const {
	Transform t{};
	if(n.transform_ != k_none)
		t = transforms_[n.transform_];

	const float s{parent.scale_ * t.scale_};
	const float w{n.frame_.width_ * s};
	const float h{n.frame_.height_ * s};
	const float px{parent.frame_.x_ + (n.frame_.x_ + n.frame_.width_ * t.pivot_x_) * parent.scale_};
	const float py{parent.frame_.y_ + (n.frame_.y_ + n.frame_.height_ * t.pivot_y_) * parent.scale_};

	return {
		.frame_ =
			{
				.x_      = px - w * t.pivot_x_,
				.y_      = py - h * t.pivot_y_,
				.width_  = w,
				.height_ = h,
			},
		.scale_ = s,
	};
}

inline RectView::operator bool() const {
	return ctx_ && ctx_->is_alive(h_);
}

inline ImageView::operator bool() const {
	return ctx_ && ctx_->is_alive(h_);
}

inline TextView::operator bool() const {
	return ctx_ && ctx_->is_alive(h_);
}

inline RingView::operator bool() const {
	return ctx_ && ctx_->is_alive(h_);
}

inline RingView& RingView::set_progress(float p) {
	if(auto* d = data())
		d->progress_ = std::clamp(p, 0.f, 1.f);
	return *this;
}

inline RingView& RingView::set_thickness(float t) {
	if(auto* d = data())
		d->thickness_ = t < 0.f ? 0.f : t;
	return *this;
}

inline RingView& RingView::set_color(const Color& c) {
	if(auto* d = data())
		d->color_ = c;
	return *this;
}

inline RingView& RingView::set_track(const Color& c) {
	if(auto* d = data())
		d->track_ = c;
	return *this;
}

inline float RingView::get_progress() const {
	auto* d{data()};
	return d ? d->progress_ : 0.f;
}

inline float RingView::get_thickness() const {
	auto* d{data()};
	return d ? d->thickness_ : 0.f;
}

inline Color RingView::get_color() const {
	auto* d{data()};
	return d ? d->color_ : Color{};
}

inline Color RingView::get_track() const {
	auto* d{data()};
	return d ? d->track_ : Color{};
}

inline RectView& RectView::set_color(const Color& c) {
	if(auto* d = data())
		d->color_ = c;
	return *this;
}

inline RectView& RectView::set_radius(float r) {
	if(auto* d = data())
		d->radius_ = r < 0.f ? 0.f : r;
	return *this;
}

inline Color RectView::get_color() const {
	auto* d{data()};
	return d ? d->color_ : Color{};
}

inline float RectView::get_radius() const {
	auto* d{data()};
	return d ? d->radius_ : 0.f;
}

inline ImageView& ImageView::set_texture(Texture t) {
	if(auto* d = data()) {
		d->texture_ = std::move(t);
		ctx_->mark_dirty(h_.id_);
	}
	return *this;
}

inline ImageView& ImageView::set_radius(float r) {
	if(auto* d = data())
		d->radius_ = r < 0.f ? 0.f : r;
	return *this;
}

inline ImageView& ImageView::set_uv(const UvRect& uv) {
	if(auto* d = data(); d && d->uv_ != uv) {
		d->uv_ = uv;
		ctx_->mark_dirty(h_.id_);
	}
	return *this;
}

inline float ImageView::get_radius() const {
	auto* d{data()};
	return d ? d->radius_ : 0.f;
}

inline float TextView::get_texture_width() const {
	const auto* d{data()};
	return d ? d->natural_w_ : 0.f;
}

inline float TextView::get_texture_height() const {
	const auto* d{data()};
	return d ? d->natural_h_ : 0.f;
}

inline TextView& TextView::set_texture(Texture t) {
	if(auto* d = data()) {
		d->raster_ = nullptr;
		d->text_.clear();
		d->set_content(std::move(t));
		ctx_->mark_dirty(h_.id_);
	}
	return *this;
}

inline TextView& TextView::set_font(FontHandle f) {
	if(auto* d = data())
		d->font_ = f;
	return *this;
}

inline TextView& TextView::set_color(const Color& c) {
	if(auto* d = data())
		d->color_ = c;
	return *this;
}

inline Color TextView::get_color() const {
	auto* d{data()};
	return d ? d->color_ : Color{};
}

}
