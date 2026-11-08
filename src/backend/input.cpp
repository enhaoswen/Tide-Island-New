#include "scene/context.hpp"

#include <algorithm>

// Pointer state machine. All hover / click / drag behaviour lives here, so a new
// interaction (double click, long press, ...) is one more branch in this file and
// never touches node storage or types.
//
//   hovered_  top-most interactive node under the pointer
//   pressed_  node that received the button-down (captures the pointer)
//   dragging_ pressed_ moved past the drag threshold and is Draggable;
//             while dragging, hover is frozen and events go to pressed_

namespace Object {

namespace {

// Point in rounded rect (radius clamped to half the shorter side).
[[nodiscard]] bool contains(float fx, float fy, float fw, float fh, float radius,
                            float px, float py) noexcept {
    if (fw <= 0.f || fh <= 0.f || px < fx || px > fx + fw || py < fy || py > fy + fh)
        return false;
    if (radius <= 0.f)
        return true;

    radius = std::min(radius, std::min(fw, fh) * .5f);
    const float dx = px - std::clamp(px, fx + radius, fx + fw - radius);
    const float dy = py - std::clamp(py, fy + radius, fy + fh - radius);
    return dx * dx + dy * dy <= radius * radius;
}

} // namespace

// Front-to-back search with early exit: children are visited last-to-first, so
// the first hit is the top-most one. Clip nodes prune their whole subtree when
// the point is outside (children cannot be visible there anyway).
uint16_t Context::hit_node(uint16_t i, float x, float y, float ox, float oy) const noexcept {
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
    for (auto it = roots_.rbegin(); it != roots_.rend(); ++it) {
        const uint16_t hit = hit_node(it->id, x, y, 0.f, 0.f);
        if (hit != kNone)
            return handle_of(hit);
    }
    return {};
}

void Context::emit(Handle target, EventType type, Button b, float dx, float dy) {
    const Node* n = node(target);
    events_.push_back(Event{target, n ? n->tag : 0u, px_, py_, dx, dy, type, b});
}

// Forget hovered / pressed nodes that were destroyed since the last call.
void Context::validate_input() noexcept {
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
    px_          = x;
    py_          = y;
    has_pointer_ = true;

    if (pressed_) {
        if (dragging_) {
            emit(pressed_, EventType::Drag, pressed_button_, dx, dy);
            return;
        }

        const Node* n = node(pressed_);
        if (n && (n->flags & flag::draggable)) {
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
            return; // one button at a time

        update_hover(); // hovered_ may be stale if the scene changed under a still pointer
        const Node* n = node(hovered_);
        if (!n || !(n->flags & (flag::clickable | flag::draggable)))
            return; // top-most interactive node is hover-only: it swallows the press

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
        update_hover(); // hover was frozen during the drag; resync
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
            continue; // destroyed between emit and poll
        out = e;
        return true;
    }
    events_.clear();
    event_head_ = 0;
    return false;
}

} // namespace UI