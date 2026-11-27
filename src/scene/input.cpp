#include <algorithm>
#include <ranges>

#include "scene/context.hpp"

using namespace std;

namespace Object {

namespace {

[[nodiscard]] bool is_inside(float fx, float fy, float fw, float fh, float radius, float px, float py) {
	if(fw <= 0.f || fh <= 0.f || px < fx || px > fx + fw || py < fy || py > fy + fh)
		return false;
	if(radius <= 0.f)
		return true;

	radius = min(radius, std::min(fw, fh) * .5f);
	const float dx{px - clamp(px, fx + radius, fx + fw - radius)};
	const float dy{py - clamp(py, fy + radius, fy + fh - radius)};
	return dx * dx + dy * dy <= radius * radius;
}

// Interaction flags a node needs for the pointer state machine to emit the types in `m`.
[[nodiscard]] constexpr uint16_t required_flags(EventMask m, uint16_t current) {
	uint16_t f = 0;
	if(m.has(EventType::Click))
		f |= Flag::k_clickable;
	if(m.has_any(EventType::HoverEnter | EventType::HoverLeave))
		f |= Flag::k_hoverable;
	if(m.has_any(EventType::DragStart | EventType::Drag | EventType::DragEnd))
		f |= Flag::k_draggable;
	// Press / Release are emitted for clickable and draggable nodes alike.
	if(m.has_any(EventType::Press | EventType::Release) && !((current | f) & (Flag::k_clickable | Flag::k_draggable)))
		f |= Flag::k_clickable;
	return f;
}

}

EventCallback Context::listener(const Node& n, const EventType type) const {
	for(uint16_t i = n.listeners_; i != k_none; i = listeners_[i].next_)
		if(listeners_[i].mask_.has(type))
			return listeners_[i].callback_;
	return {};
}

void Context::unbind(Node& n, const EventMask mask) {
	uint16_t* link{&n.listeners_};
	while(*link != k_none) {
		Listener& l{listeners_[*link]};
		l.mask_ = l.mask_.without(mask);
		if(l.mask_) {
			link = &l.next_;
			continue;
		}
		const uint16_t dead{*link};
		*link = l.next_;
		listeners_.release(dead); // only touches the free list; `link` stays valid
	}
}

bool Context::on(const Handle h, const EventMask mask, const EventCallback cb) {
	Node* n{node(h)};
	if(!n || !mask)
		return false;

	unbind(*n, mask);
	if(!cb)
		return true;

	const uint16_t l{listeners_.acquire(Listener{cb, n->listeners_, mask})};
	if(l == k_none)
		return false;

	n->listeners_ = l;
	n->flags_ |= required_flags(mask, n->flags_);
	return true;
}

void Context::off(const Handle h, const EventMask mask) {
	if(Node* n = node(h))
		unbind(*n, mask);
}

bool Context::dispatch_events() {
	bool  has_run{false};
	Event e;
	while(poll_event(e)) {
		// Resolved now rather than at emit time, so earlier callbacks may unbind later ones.
		// The callback is copied out, so it may destroy its own node / binding.
		if(const EventCallback cb = listener(nodes_[e.target_.id_], e.type_)) {
			cb(e);
			has_run = true;
		}
	}
	return has_run;
}

uint16_t Context::hit_node(uint16_t i, float x, float y, const Placed& parent) const {
	const Node& n{nodes_[i]};
	if(!(n.flags_ & Flag::k_visible) || n.opacity_ == 0)
		return k_none; // what you cannot see you cannot click

	const Placed p{place(n, parent)};
	const Frame& f{p.frame_};

	const bool  is_clipped{(n.flags_ & Flag::k_clip) != 0};
	const bool  is_self{(n.flags_ & Flag::k_interactive) != 0};
	const float radius{(is_clipped || is_self) ? radius_of(n) * p.scale_ : 0.f};

	bool is_contained{false};
	if(is_clipped || is_self) {
		is_contained = is_inside(f.x_, f.y_, f.width_, f.height_, radius, x, y);
		if(is_clipped && !is_contained)
			return k_none;
	}

	for(uint16_t c = n.last_; c != k_none; c = nodes_[c].prev_) {
		const uint16_t hit{hit_node(c, x, y, p)};
		if(hit != k_none)
			return hit;
	}
	return (is_self && is_contained) ? i : k_none;
}

Handle Context::hit_test(float x, float y) const {
	for(const auto [id, gen] : views::reverse(roots_)) {
		const uint16_t hit{hit_node(id, x, y, Placed{})};
		if(hit != k_none)
			return handle_of(hit);
	}
	return {};
}

void Context::emit(const Handle target, const EventType type, const Button b, const float dx, const float dy) {
	const Node* n{node(target)};
	if(!n || !listener(*n, type))
		return; // nobody listens: don't queue

	// Pointer motion can outpace the frame loop; fold consecutive undispatched drags into one.
	if(type == EventType::Drag && event_head_ < events_.size()) {
		if(Event& last = events_.back(); last.type_ == EventType::Drag && last.target_ == target) {
			last.x_ = px_;
			last.y_ = py_;
			last.dx_ += dx;
			last.dy_ += dy;
			return;
		}
	}

	events_.push_back(Event{target, n->tag_, px_, py_, dx, dy, type, b});
}

void Context::validate_input() {
	if(hovered_ && !is_alive(hovered_))
		hovered_ = {};
	if(pressed_ && !is_alive(pressed_)) {
		pressed_     = {};
		is_dragging_ = false;
	}
}

void Context::update_hover() {
	if(is_dragging_ || !has_pointer_)
		return;

	validate_input();
	const Handle h{hit_test(px_, py_)};
	if(h == hovered_)
		return;

	if(const Node* old = node(hovered_); old && (old->flags_ & Flag::k_hoverable))
		emit(hovered_, EventType::HoverLeave, Button::None, 0.f, 0.f);

	hovered_ = h;

	if(const Node* now = node(h); now && (now->flags_ & Flag::k_hoverable))
		emit(h, EventType::HoverEnter, Button::None, 0.f, 0.f);
}

void Context::pointer_move(float x, float y) {
	validate_input();

	const float dx{x - px_};
	const float dy{y - py_};
	px_          = x;
	py_          = y;
	has_pointer_ = true;

	if(pressed_) {
		if(is_dragging_) {
			emit(pressed_, EventType::Drag, pressed_button_, dx, dy);
			return;
		}

		if(const Node* n = node(pressed_); n && (n->flags_ & Flag::k_draggable)) {
			const float tx{x - press_x_};
			const float ty{y - press_y_};
			if(tx * tx + ty * ty >= drag_threshold_ * drag_threshold_) {
				is_dragging_ = true;
				emit(pressed_, EventType::DragStart, pressed_button_, tx, ty);
				return;
			}
		}
	}

	update_hover();
}

void Context::pointer_button(Button b, bool is_down) {
	validate_input();
	if(!has_pointer_ || b == Button::None)
		return;

	if(is_down) {
		if(pressed_)
			return;

		update_hover();
		if(const Node* n = node(hovered_); !n || !(n->flags_ & (Flag::k_clickable | Flag::k_draggable)))
			return;

		pressed_        = hovered_;
		pressed_button_ = b;
		press_x_        = px_;
		press_y_        = py_;
		is_dragging_    = false;
		emit(pressed_, EventType::Press, b, 0.f, 0.f);
		return;
	}

	if(!pressed_ || b != pressed_button_)
		return;

	const Handle p{pressed_};
	pressed_ = {};

	if(is_dragging_) {
		is_dragging_ = false;
		emit(p, EventType::DragEnd, b, 0.f, 0.f);
		update_hover();
		return;
	}

	update_hover();
	emit(p, EventType::Release, b, 0.f, 0.f);

	const Node* n{node(p)};
	if(n && (n->flags_ & Flag::k_clickable) && hovered_ == p)
		emit(p, EventType::Click, b, 0.f, 0.f);
}

void Context::pointer_cancel() {
	validate_input();

	if(pressed_ && is_dragging_)
		emit(pressed_, EventType::DragEnd, pressed_button_, 0.f, 0.f);
	pressed_     = {};
	is_dragging_ = false;

	if(const Node* h = node(hovered_); h && (h->flags_ & Flag::k_hoverable))
		emit(hovered_, EventType::HoverLeave, Button::None, 0.f, 0.f);
	hovered_     = {};
	has_pointer_ = false;
}

bool Context::poll_event(Event& out) {
	while(event_head_ < events_.size()) {
		const Event e{events_[event_head_++]};
		if(!is_alive(e.target_))
			continue;
		out = e;
		return true;
	}
	events_.clear();
	event_head_ = 0;
	return false;
}

}
