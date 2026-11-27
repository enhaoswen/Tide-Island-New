#include "context.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "graphics/renderer.hpp"
#include "graphics/text.hpp"

using namespace std;

namespace Object {

namespace {
[[nodiscard]] uint8_t quantize_opacity(float v) {
	v = clamp(v, 0.f, 1.f);
	return static_cast<uint8_t>(v * 255.f + 0.5f);
}

}

Context::Context(size_t reserve_nodes) {
	nodes_.reserve(min<size_t>(reserve_nodes, k_none));
}

uint16_t Context::alloc_node() {
	uint16_t i;
	if(free_head_ != k_none) {
		i          = free_head_;
		free_head_ = nodes_[i].next_;
	} else {
		if(nodes_.size() >= k_none)
			return k_none;
		i = static_cast<uint16_t>(nodes_.size());
		nodes_.emplace_back();
	}

	Node&          n{nodes_[i]};
	const uint16_t gen{n.gen_};
	n      = Node{};
	n.gen_ = gen;
	++live_;
	return i;
}

void Context::release_data(Kind kind, uint16_t data) {
	if(data == k_none)
		return;
	switch(kind) {
		case Kind::Group:
			break;
		case Kind::Rect:
			rects_.release(data);
			break;
		case Kind::Image:
			images_.release(data);
			break;
		case Kind::Text:
			texts_.release(data);
			break;
		case Kind::Ring:
			rings_.release(data);
			break;
	}
}

void Context::release_node(uint16_t i) {
	Node& n{nodes_[i]};
	release_data(n.kind_, n.data_);
	if(n.layout_ != k_none)
		layouts_.release(n.layout_);

	unbind(n, EventMask::all());

	if(n.transform_ != k_none)
		transforms_.release(n.transform_);

	if(!grow_.empty())
		grow_.erase(i);

	const auto next_gen{static_cast<uint16_t>(n.gen_ + 1)};

	n          = Node{};
	n.gen_     = next_gen;
	n.next_    = free_head_;
	free_head_ = i;
	--live_;
}

Handle Context::spawn(Kind kind, uint16_t data, const Frame& frame, Handle parent) {
	const uint16_t i{alloc_node()};
	if(i == k_none) {
		release_data(kind, data);
		return {};
	}

	Node& n{nodes_[i]};
	n.kind_  = kind;
	n.data_  = data;
	n.frame_ = frame;
	if(kind == Kind::Image || kind == Kind::Text) {
		n.flags_ = with_size_mode(n.flags_, Axis::Horizontal, SizeMode::Fit);
		n.flags_ = with_size_mode(n.flags_, Axis::Vertical, SizeMode::Fit);
	}

	const Handle h{handle_of(i)};
	if(parent && !attach(h, parent)) {
		release_node(i);
		return {};
	}
	return h;
}

Handle Context::create_root(float width, float height) {
	const uint16_t i{alloc_node()};
	if(i == k_none)
		return {};

	Node& n{nodes_[i]};
	n.flags_ |= Flag::k_root;
	n.frame_.width_  = width;
	n.frame_.height_ = height;

	const Handle h{handle_of(i)};
	roots_.push_back(h);
	return h;
}

Handle Context::create_group(Handle parent, const Frame& frame) {
	return spawn(Kind::Group, k_none, frame, parent);
}

Handle Context::create_rect(Handle parent, const RectDesc& d) {
	const uint16_t data{rects_.acquire(RectData{d.color_, d.radius_})};
	if(data == k_none)
		return {};
	return spawn(Kind::Rect, data, d.frame_, parent);
}

Handle Context::create_image(Handle parent, ImageDesc d) {
	const uint16_t data{images_.acquire(ImageData{.texture_ = std::move(d.texture_), .radius_ = d.radius_})};
	if(data == k_none)
		return {};
	return spawn(Kind::Image, data, d.frame_, parent);
}

Handle Context::create_text(Handle parent, TextDesc d) {
	TextData td{};
	td.font_  = d.font_;
	td.color_ = d.color_;
	td.set_content(std::move(d.texture_));
	const uint16_t data{texts_.acquire(std::move(td))};
	if(data == k_none)
		return {};
	return spawn(Kind::Text, data, d.frame_, parent);
}

Handle Context::create_ring(Handle parent, const RingDesc& d) {
	Color track{d.track_.value_or(d.color_)};
	if(!d.track_)
		color_channel(track, 3) *= .25f;
	const uint16_t data =
		rings_.acquire(RingData{d.color_, track, clamp(d.progress_, 0.f, 1.f), max(d.thickness_, 0.f)});
	if(data == k_none)
		return {};
	return spawn(Kind::Ring, data, d.frame_, parent);
}

void Context::destroy(Handle h) {
	Node* n{node(h)};
	if(!n)
		return;

	const uint16_t root{h.id_};

	if(n->flags_ & Flag::k_root) {
		erase(roots_, h);
	} else if(n->parent_ != k_none) {
		const uint16_t old_parent{unlink(root)};
		mark_dirty(old_parent);
	}

	uint16_t cur{root};
	for(;;) {
		Node& c{nodes_[cur]};
		if(c.first_ != k_none) {
			cur = c.first_;
			continue;
		}

		const uint16_t up{(cur == root) ? k_none : c.parent_};
		if(up != k_none)
			nodes_[up].first_ = c.next_;

		release_node(cur);

		if(up == k_none)
			break;
		cur = up;
	}
}

void Context::clear() {
	while(!roots_.empty())
		destroy(roots_.back());
}

void Context::link_last(uint16_t parent, uint16_t child) {
	Node& p{nodes_[parent]};
	Node& c{nodes_[child]};

	c.parent_ = parent;
	c.next_   = k_none;
	c.prev_   = p.last_;
	if(p.last_ != k_none)
		nodes_[p.last_].next_ = child;
	else
		p.first_ = child;
	p.last_ = child;
}

void Context::link_first(uint16_t parent, uint16_t child) {
	Node& p{nodes_[parent]};
	Node& c{nodes_[child]};

	c.parent_ = parent;
	c.prev_   = k_none;
	c.next_   = p.first_;
	if(p.first_ != k_none)
		nodes_[p.first_].prev_ = child;
	else
		p.last_ = child;
	p.first_ = child;
}

uint16_t Context::unlink(uint16_t child) {
	Node&          c{nodes_[child]};
	const uint16_t parent{c.parent_};
	if(parent == k_none)
		return k_none;

	Node& p{nodes_[parent]};
	if(c.prev_ != k_none)
		nodes_[c.prev_].next_ = c.next_;
	else
		p.first_ = c.next_;

	if(c.next_ != k_none)
		nodes_[c.next_].prev_ = c.prev_;
	else
		p.last_ = c.prev_;

	c.parent_ = c.prev_ = c.next_ = k_none;
	return parent;
}

void Context::mark_dirty(uint16_t i) {
	while(i != k_none) {
		Node& n{nodes_[i]};
		if(n.flags_ & Flag::k_dirty)
			return;
		n.flags_ |= Flag::k_dirty;
		i = n.parent_;
	}
}

bool Context::attach(Handle child, Handle parent) {
	Node* c{node(child)};
	Node* p{node(parent)};
	if(!c || !p || child == parent || (c->flags_ & Flag::k_root))
		return false;

	for(uint16_t a = parent.id_; a != k_none; a = nodes_[a].parent_)
		if(a == child.id_)
			return false;

	if(c->parent_ != k_none)
		mark_dirty(unlink(child.id_));

	link_last(parent.id_, child.id_);
	mark_dirty(parent.id_);
	return true;
}

bool Context::detach(Handle h) {
	Node* n{node(h)};
	if(!n || n->parent_ == k_none)
		return false;
	mark_dirty(unlink(h.id_));
	return true;
}

void Context::move_to_front(Handle h) {
	Node* n{node(h)};
	if(!n)
		return;

	if(n->flags_ & Flag::k_root) {
		if(const auto it = find(roots_.begin(), roots_.end(), h); it != roots_.end())
			rotate(it, it + 1, roots_.end());
		return;
	}
	if(n->parent_ == k_none || n->next_ == k_none)
		return;

	const uint16_t p{unlink(h.id_)};
	link_last(p, h.id_);
	if(nodes_[p].layout_ != k_none)
		mark_dirty(p);
}

void Context::move_to_back(Handle h) {
	Node* n{node(h)};
	if(!n)
		return;

	if(n->flags_ & Flag::k_root) {
		if(const auto it = find(roots_.begin(), roots_.end(), h); it != roots_.end())
			rotate(roots_.begin(), it, it + 1);
		return;
	}
	if(n->parent_ == k_none || n->prev_ == k_none)
		return;

	const uint16_t p{unlink(h.id_)};
	link_first(p, h.id_);
	if(nodes_[p].layout_ != k_none)
		mark_dirty(p);
}

Handle Context::parent(Handle h) const {
	const Node* n{node(h)};
	return n && n->parent_ != k_none ? handle_of(n->parent_) : Handle{};
}

Context::ChildRange Context::children(Handle h) const {
	const Node* n{node(h)};
	return {this, n ? n->first_ : k_none};
}

void Context::set_pos(Handle h, float x, float y) {
	if(Node* n = node(h)) {
		n->frame_.x_ = x;
		n->frame_.y_ = y;
	}
}

void Context::set_size(Handle h, float w, float hgt) {
	if(Node* n = node(h); n && (n->frame_.width_ != w || n->frame_.height_ != hgt)) {
		n->frame_.width_  = w;
		n->frame_.height_ = hgt;
		mark_dirty(h.id_);
	}
}

void Context::set_size_mode(Handle h, Axis axis, SizeMode mode) {
	if(Node* n = node(h); n && size_mode(n->flags_, axis) != mode) {
		n->flags_ = with_size_mode(n->flags_, axis, mode);
		mark_dirty(h.id_);
	}
}

void Context::set_grow(Handle h, Axis axis, const GrowParams& params) {
	Node* n{node(h)};
	if(!n)
		return;

	GrowParams p{params};
	p.weight_ = max(p.weight_, 0.f);
	p.min_    = max(p.min_, 0.f);
	p.max_    = max(p.max_, p.min_);
	GrowData& d{grow_[h.id_]};
	(axis == Axis::Horizontal ? d.h_ : d.v_) = p;
	n->flags_                                = with_size_mode(n->flags_, axis, SizeMode::Grow);
	mark_dirty(h.id_);
}

void Context::set_flag(Handle h, uint16_t bit, bool is_on) {
	if(Node* n = node(h))
		n->flags_ = static_cast<uint16_t>(is_on ? (n->flags_ | bit) : (n->flags_ & ~bit));
}

void Context::set_visible(Handle h, bool is_visible) {
	if(Node* n = node(h); n && ((n->flags_ & Flag::k_visible) != 0) != is_visible) {
		set_flag(h, Flag::k_visible, is_visible);
		mark_dirty(h.id_);
	}
}

void Context::set_opacity(Handle h, float v) {
	if(Node* n = node(h))
		n->opacity_ = quantize_opacity(v);
}

void Context::set_clip(Handle h, bool is_enabled) {
	set_flag(h, Flag::k_clip, is_enabled);
}

void Context::set_clickable(Handle h, bool is_enabled) {
	set_flag(h, Flag::k_clickable, is_enabled);
}

void Context::set_hoverable(Handle h, bool is_enabled) {
	set_flag(h, Flag::k_hoverable, is_enabled);
}

void Context::set_draggable(Handle h, bool is_enabled) {
	set_flag(h, Flag::k_draggable, is_enabled);
}

void Context::set_tag(Handle h, uint32_t tag) {
	if(Node* n = node(h))
		n->tag_ = tag;
}

void Context::set_layout(Handle h, const LayoutData& layout) {
	Node* n{node(h)};
	if(!n)
		return;

	if(n->layout_ == k_none) {
		const uint16_t idx{layouts_.acquire(layout)};
		if(idx == k_none)
			return;
		n->layout_ = idx;
	} else {
		layouts_[n->layout_] = layout;
	}
	mark_dirty(h.id_);
}

void Context::clear_layout(Handle h) {
	if(Node* n = node(h); n && n->layout_ != k_none) {
		layouts_.release(n->layout_);
		n->layout_ = k_none;
		mark_dirty(h.id_);
	}
}

Frame Context::frame(Handle h) const {
	const Node* n{node(h)};
	return n ? n->frame_ : Frame{};
}

Frame Context::absolute_frame(Handle h) const {
	return node(h) ? placed(h.id_).frame_ : Frame{};
}

Placed Context::placed(uint16_t i) const {
	const Node& n{nodes_[i]};
	return place(n, n.parent_ == k_none ? Placed{} : placed(n.parent_));
}

Transform& Context::transform_of(Node& n) {
	if(n.transform_ == k_none)
		n.transform_ = transforms_.acquire({});
	return transforms_[n.transform_];
}

void Context::set_scale(Handle h, float s) {
	set_prop(h, Prop::Scale, s);
}

void Context::set_pivot(Handle h, float x, float y) {
	if(Node* n = node(h)) {
		Transform& t{transform_of(*n)};
		t.pivot_x_ = x;
		t.pivot_y_ = y;
	}
}

float Context::scale(Handle h) const {
	return get_prop(h, Prop::Scale).value_or(1.f);
}

uint32_t Context::tag(Handle h) const {
	const Node* n{node(h)};
	return n ? n->tag_ : 0;
}

float Context::radius_of(const Node& n) const {
	switch(n.kind_) {
		case Kind::Rect:
			return rects_[n.data_].radius_;
		case Kind::Image:
			return images_[n.data_].radius_;
		default:
			return 0.f;
	}
}

optional<float> Context::get_prop(Handle h, Prop p) const {
	const Node* n{node(h)};
	if(!n)
		return nullopt;

	switch(p) {
		case Prop::X:
			return n->frame_.x_;
		case Prop::Y:
			return n->frame_.y_;
		case Prop::W:
			return n->frame_.width_;
		case Prop::H:
			return n->frame_.height_;
		case Prop::Opacity:
			return static_cast<float>(n->opacity_) * (1.f / 255.f);
		case Prop::Scale:
			return n->transform_ == k_none ? 1.f : transforms_[n->transform_].scale_;
		case Prop::Radius:
			if(n->kind_ == Kind::Rect || n->kind_ == Kind::Image)
				return radius_of(*n);
			return nullopt;
		case Prop::ColorR:
		case Prop::ColorG:
		case Prop::ColorB:
		case Prop::ColorA: {
			const unsigned ch{static_cast<unsigned>(p) - static_cast<unsigned>(Prop::ColorR)};
			if(n->kind_ == Kind::Rect)
				return color_channel(rects_[n->data_].color_, ch);
			if(n->kind_ == Kind::Text)
				return color_channel(texts_[n->data_].color_, ch);
			if(n->kind_ == Kind::Ring)
				return color_channel(rings_[n->data_].color_, ch);
			return nullopt;
		}
		case Prop::Progress:
			if(n->kind_ == Kind::Ring)
				return rings_[n->data_].progress_;
			return nullopt;
		case Prop::Thickness:
			if(n->kind_ == Kind::Ring)
				return rings_[n->data_].thickness_;
			return nullopt;
	}
	return nullopt;
}

bool Context::set_prop(Handle h, Prop p, float v) {
	Node* n{node(h)};
	if(!n)
		return false;

	switch(p) {
		case Prop::X:
			n->frame_.x_ = v;
			return true;
		case Prop::Y:
			n->frame_.y_ = v;
			return true;
		case Prop::W:
			v = max(v, 0.f);
			if(n->frame_.width_ != v) {
				n->frame_.width_ = v;
				mark_dirty(h.id_);
			}
			return true;
		case Prop::H:
			v = max(v, 0.f);
			if(n->frame_.height_ != v) {
				n->frame_.height_ = v;
				mark_dirty(h.id_);
			}
			return true;
		case Prop::Opacity:
			n->opacity_ = quantize_opacity(v);
			return true;
		case Prop::Scale:
			transform_of(*n).scale_ = max(v, 0.f);
			return true;
		case Prop::Radius:
			if(n->kind_ == Kind::Rect) {
				rects_[n->data_].radius_ = max(v, 0.f);
				return true;
			}
			if(n->kind_ == Kind::Image) {
				images_[n->data_].radius_ = max(v, 0.f);
				return true;
			}
			return false;
		case Prop::ColorR:
		case Prop::ColorG:
		case Prop::ColorB:
		case Prop::ColorA: {
			const unsigned ch{static_cast<unsigned>(p) - static_cast<unsigned>(Prop::ColorR)};
			if(n->kind_ == Kind::Rect) {
				color_channel(rects_[n->data_].color_, ch) = v;
				return true;
			}
			if(n->kind_ == Kind::Text) {
				color_channel(texts_[n->data_].color_, ch) = v;
				return true;
			}
			if(n->kind_ == Kind::Ring) {
				color_channel(rings_[n->data_].color_, ch) = v;
				return true;
			}
			return false;
		}
		case Prop::Progress:
			if(n->kind_ != Kind::Ring)
				return false;
			// Springs overshoot; clamping keeps the arc from wrapping past full or vanishing.
			rings_[n->data_].progress_ = clamp(v, 0.f, 1.f);
			return true;
		case Prop::Thickness:
			if(n->kind_ != Kind::Ring)
				return false;
			rings_[n->data_].thickness_ = max(v, 0.f);
			return true;
	}
	return false;
}

RectView Context::rect(Handle h) {
	const Node* n{node(h)};
	return n && n->kind_ == Kind::Rect ? RectView{this, h} : RectView{};
}

ImageView Context::image(Handle h) {
	const Node* n{node(h)};
	return n && n->kind_ == Kind::Image ? ImageView{this, h} : ImageView{};
}

RingView Context::ring(Handle h) {
	const Node* n{node(h)};
	return n && n->kind_ == Kind::Ring ? RingView{this, h} : RingView{};
}

TextView Context::text(Handle h) {
	const Node* n{node(h)};
	return n && n->kind_ == Kind::Text ? TextView{this, h} : TextView{};
}

bool Context::animate(SAnimDesc desc) {
	return anim_.start(*this, desc.h_, desc.p_, desc.to_, desc.s_, desc.delay_, std::move(desc.on_done_));
}

bool Context::animate(TAnimDesc desc) {
	return anim_.start(*this, desc.h_, desc.p_, desc.to_, desc.t_, desc.delay_, std::move(desc.on_done_));
}

void Context::update(float dt) {
	const bool has_animated{anim_.update(*this, dt)};
	const bool is_laid_out{layout()};
	if(has_animated || is_laid_out)
		update_hover();
}

float Context::text_zoom(uint16_t i) const {
	const TextData& d{texts_[nodes_[i].data_]};
	const Placed    p{placed(i)};
	const float     z{d.natural_w_ > 0.f ? p.frame_.width_ / d.natural_w_ : p.scale_};
	return z > 0.f ? z : 1.f;
}

uint64_t Context::raster_key(const TextData& d, float scale, int phase_x, int phase_y) {
	const uint64_t size =
		d.raster_ ? static_cast<uint64_t>(lround(scale * 1024.f)) + 1 : Text::raster_size(d.font_, scale);
	return size ? size << 4 | static_cast<uint64_t>(phase_x) << 2 | static_cast<uint64_t>(phase_y) : 0;
}

Texture Context::raster_text(const TextData& d, float scale, float sub_x, float sub_y) {
	if(d.raster_)
		return d.raster_(scale, sub_x, sub_y, d.raster_arg_);
	if(!d.text_.empty())
		return Renderer::prepare_text(d.text_, d.font_, scale, sub_x, sub_y);
	return {};
}

void Context::rasterize(uint16_t i, TextData& d) {
	const float want{Renderer::text_scale() * text_zoom(i)};
	d.set_content(raster_text(d, want, 0.f, 0.f));
	d.raster_key_ = raster_key(d, want, 0, 0);
}

Frame Context::prepare_text_draw(TextData& d, const Frame& abs) {
	if(!d.texture_.is_valid() || (!d.raster_ && d.text_.empty()))
		return abs;

	const float scale{Renderer::text_scale()};
	const float zoom{d.natural_w_ > 0.f ? abs.width_ / d.natural_w_ : 1.f};
	const float want{scale * zoom};

	const auto split = [](float v, float& whole) {
		const float quarters{round(v * 4.f)};
		whole = floor(quarters / 4.f);
		return quarters / 4.f - whole;
	};
	float       whole_x{}, whole_y{};
	const float sub_x{split((abs.x_ - d.natural_origin_x_ * zoom) * scale, whole_x)};
	const float sub_y{split((abs.y_ - d.natural_origin_y_ * zoom) * scale, whole_y)};

	const uint64_t key{raster_key(d, want, static_cast<int>(sub_x * 4.f), static_cast<int>(sub_y * 4.f))};
	if(key != 0 && key != d.raster_key_) {
		if(Texture t = raster_text(d, want, sub_x, sub_y); t.is_valid()) {
			d.texture_    = std::move(t);
			d.raster_key_ = key;
		}
	}

	// The key may keep a raster from a zoom a hair away; still draw it 1:1 instead of blurring.
	const float k{std::abs(d.texture_.get_scale() - want) <= want * 0.01f ? d.texture_.get_scale() / scale : zoom};
	return {
		.x_      = whole_x / scale + d.texture_.get_origin_x() * k,
		.y_      = whole_y / scale + d.texture_.get_origin_y() * k,
		.width_  = d.texture_.get_logical_width() * k,
		.height_ = d.texture_.get_logical_height() * k,
	};
}

ImageView& ImageView::set_image(string_view path) {
	if(auto* d = data()) {
		d->texture_ = Renderer::prepare_image(path);
		ctx_->mark_dirty(h_.id_);
	}
	return *this;
}

TextView& TextView::set_raster(RasterFn fn, uintptr_t arg) {
	if(auto* d = data()) {
		d->raster_     = fn;
		d->raster_arg_ = arg;
		d->text_.clear();
		ctx_->rasterize(h_.id_, *d);
		ctx_->mark_dirty(h_.id_);
	}
	return *this;
}

TextView& TextView::set_text(string_view s) {
	if(auto* d = data()) {
		d->raster_ = nullptr;
		d->text_   = s;
		ctx_->rasterize(h_.id_, *d);
		ctx_->mark_dirty(h_.id_);
	}
	return *this;
}

}
