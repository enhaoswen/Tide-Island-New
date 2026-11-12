#include "scene/context.hpp"

#include <algorithm>
#include <ranges>

using namespace std;

namespace Object {

namespace {

[[nodiscard]] bool contains(float fx, float fy, float fw, float fh, float radius,
    float                         px, float py) {
    if (fw <= 0.f || fh <= 0.f || px < fx || px > fx + fw || py < fy || py > fy + fh)
        return false;
    if (radius <= 0.f)
        return true;

    radius         = min(radius, std::min(fw, fh) * .5f);
    const float dx = px - clamp(px, fx + radius, fx + fw - radius);
    const float dy = py - clamp(py, fy + radius, fy + fh - radius);
    return dx * dx + dy * dy <= radius * radius;
}

// Interaction flags a node needs for the pointer state machine to emit the types in `m`.
[[nodiscard]] constexpr uint16_t required_flags(EventMask m, uint16_t current) noexcept {
    uint16_t f = 0;
    if (m.has(EventType::Click))
        f |= flag::clickable;
    if (m.any(EventType::HoverEnter | EventType::HoverLeave))
        f |= flag::hoverable;
    if (m.any(EventType::DragStart | EventType::Drag | EventType::DragEnd))
        f |= flag::draggable;
    // Press / Release are emitted for clickable and draggable nodes alike.
    if (m.any(EventType::Press | EventType::Release) && !((current | f) & (flag::clickable | flag::draggable)))
        f |= flag::clickable;
    return f;
}

}

// ============================================================================
// Callback binding & dispatch
// ============================================================================

EventCallback Context::listener(const Node& n, const EventType type) const {
    for (uint16_t i = n.listeners; i != kNone; i = listeners_[i].next)
        if (listeners_[i].mask.has(type))
            return listeners_[i].callback;
    return {};
}

void Context::unbind(Node& n, const EventMask mask) {
    uint16_t* link = &n.listeners;
    while (*link != kNone) {
        Listener& l = listeners_[*link];
        l.mask      = l.mask.without(mask);
        if (l.mask) {
            link = &l.next;
            continue;
        }
        const uint16_t dead = *link;
        *link               = l.next;
        listeners_.release(dead); // only touches the free list; `link` stays valid
    }
}

bool Context::on(const Handle h, const EventMask mask, const EventCallback cb) {
    Node* n = node(h);
    if (!n || !mask)
        return false;

    unbind(*n, mask);
    if (!cb)
        return true;

    const uint16_t l = listeners_.acquire(Listener{cb, n->listeners, mask});
    if (l == kNone)
        return false;

    n->listeners = l;
    n->flags     |= required_flags(mask, n->flags);
    return true;
}

void Context::off(const Handle h, const EventMask mask) {
    if (Node* n = node(h))
        unbind(*n, mask);
}

bool Context::dispatch_events() {
    bool  ran = false;
    Event e;
    while (poll_event(e)) {
        // Resolved now rather than at emit time, so earlier callbacks may unbind later ones.
        // The callback is copied out, so it may destroy its own node / binding.
        if (const EventCallback cb = listener(nodes_[e.target.id], e.type)) {
            cb(e);
            ran = true;
        }
    }
    return ran;
}

// ============================================================================
// Pointer state machine
// ============================================================================

uint16_t Context::hit_node(uint16_t i, float x, float y, float ox, float oy) const {
    const Node& n = nodes_[i];
    if (!(n.flags & flag::visible) || n.opacity == 0)
        return kNone; // what you cannot see you cannot click

    const float ax = ox + n.frame.x;
    const float ay = oy + n.frame.y;

    const bool  clip   = (n.flags & flag::clip) != 0;
    const bool  self   = (n.flags & flag::interactive) != 0;
    const float radius = (clip || self) ? radius_of(n) : 0.f;

    bool inside = false;
    if (clip || self) {
        inside = contains(ax, ay, n.frame.width, n.frame.height, radius, x, y);
        if (clip && !inside)
            return kNone;
    }

    for (uint16_t c = n.last; c != kNone; c = nodes_[c].prev) {
        const uint16_t hit = hit_node(c, x, y, ax, ay);
        if (hit != kNone)
            return hit;
    }
    return (self && inside) ? i : kNone;
}

Handle Context::hit_test(float x, float y) const {
    for (const auto [id, gen] : views::reverse(roots_)) {
        const uint16_t hit = hit_node(id, x, y, 0.f, 0.f);
        if (hit != kNone)
            return handle_of(hit);
    }
    return {};
}

void Context::emit(const Handle target, const EventType type, const Button b, const float dx, const float dy) {
    const Node* n = node(target);
    if (!n || !listener(*n, type))
        return; // nobody listens: don't queue

    // Pointer motion can outpace the frame loop; fold consecutive undispatched drags into one.
    if (type == EventType::Drag && event_head_ < events_.size()) {
        if (Event& last = events_.back(); last.type == EventType::Drag && last.target == target) {
            last.x  = px_;
            last.y  = py_;
            last.dx += dx;
            last.dy += dy;
            return;
        }
    }

    events_.push_back(Event{target, n->tag, px_, py_, dx, dy, type, b});
}

void Context::validate_input() {
    if (hovered_ && !alive(hovered_))
        hovered_ = {};
    if (pressed_ && !alive(pressed_)) {
        pressed_  = {};
        dragging_ = false;
    }
}

void Context::update_hover() {
    if (dragging_ || !has_pointer_)
        return;

    validate_input();
    const Handle h = hit_test(px_, py_);
    if (h == hovered_)
        return;

    if (const Node* old = node(hovered_); old && (old->flags & flag::hoverable))
        emit(hovered_, EventType::HoverLeave, Button::None, 0.f, 0.f);

    hovered_ = h;

    if (const Node* now = node(h); now && (now->flags & flag::hoverable))
        emit(h, EventType::HoverEnter, Button::None, 0.f, 0.f);
}

void Context::pointer_move(float x, float y) {
    validate_input();

    const float dx = x - px_;
    const float dy = y - py_;
    px_            = x;
    py_            = y;
    has_pointer_   = true;

    if (pressed_) {
        if (dragging_) {
            emit(pressed_, EventType::Drag, pressed_button_, dx, dy);
            return;
        }

        if (const Node* n = node(pressed_); n && (n->flags & flag::draggable)) {
            const float tx = x - press_x_;
            const float ty = y - press_y_;
            if (tx * tx + ty * ty >= drag_threshold_ * drag_threshold_) {
                dragging_ = true;
                emit(pressed_, EventType::DragStart, pressed_button_, tx, ty);
                return;
            }
        }
    }

    update_hover();
}

void Context::pointer_button(Button b, bool down) {
    validate_input();
    if (!has_pointer_ || b == Button::None)
        return;

    if (down) {
        if (pressed_)
            return;

        update_hover();
        if (const Node* n = node(hovered_); !n || !(n->flags & (flag::clickable | flag::draggable)))
            return;

        pressed_        = hovered_;
        pressed_button_ = b;
        press_x_        = px_;
        press_y_        = py_;
        dragging_       = false;
        emit(pressed_, EventType::Press, b, 0.f, 0.f);
        return;
    }

    if (!pressed_ || b != pressed_button_)
        return;

    const Handle p = pressed_;
    pressed_       = {};

    if (dragging_) {
        dragging_ = false;
        emit(p, EventType::DragEnd, b, 0.f, 0.f);
        update_hover();
        return;
    }

    update_hover();
    emit(p, EventType::Release, b, 0.f, 0.f);

    const Node* n = node(p);
    if (n && (n->flags & flag::clickable) && hovered_ == p)
        emit(p, EventType::Click, b, 0.f, 0.f);
}

void Context::pointer_cancel() {
    validate_input();

    if (pressed_ && dragging_)
        emit(pressed_, EventType::DragEnd, pressed_button_, 0.f, 0.f);
    pressed_  = {};
    dragging_ = false;

    if (const Node* h = node(hovered_); h && (h->flags & flag::hoverable))
        emit(hovered_, EventType::HoverLeave, Button::None, 0.f, 0.f);
    hovered_     = {};
    has_pointer_ = false;
}

bool Context::poll_event(Event& out) {
    while (event_head_ < events_.size()) {
        const Event e = events_[event_head_++];
        if (!alive(e.target))
            continue;
        out = e;
        return true;
    }
    events_.clear();
    event_head_ = 0;
    return false;
}

}