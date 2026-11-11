#include "context.hpp"

#include "graphics/renderer.hpp"

#include <algorithm>

using namespace std;

namespace Object {

namespace {
[[nodiscard]] uint8_t quantize_opacity(float v) {
    v = clamp(v, 0.f, 1.f);
    return static_cast<uint8_t>(v * 255.f + 0.5f);
}

}

Context::Context(size_t reserve_nodes) {
    nodes_.reserve(min<size_t>(reserve_nodes, kNone));
}

uint16_t Context::alloc_node() {
    uint16_t i;
    if (free_head_ != kNone) {
        i          = free_head_;
        free_head_ = nodes_[i].next;
    }
    else {
        if (nodes_.size() >= kNone)
            return kNone;
        i = static_cast<uint16_t>(nodes_.size());
        nodes_.emplace_back();
    }

    Node&          n   = nodes_[i];
    const uint16_t gen = n.gen;
    n                  = Node{};
    n.gen              = gen;
    ++live_;
    return i;
}

void Context::release_data(Kind kind, uint16_t data) {
    if (data == kNone)
        return;
    switch (kind) {
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
    }
}

void Context::release_node(uint16_t i) {
    Node& n = nodes_[i];
    release_data(n.kind, n.data);
    if (n.layout != kNone)
        layouts_.release(n.layout);

    if (!grow_.empty())
        grow_.erase(i);

    const auto next_gen = static_cast<uint16_t>(n.gen + 1);

    n          = Node{};
    n.gen      = next_gen;
    n.next     = free_head_;
    free_head_ = i;
    --live_;
}

Handle Context::spawn(Kind kind, uint16_t data, const Frame& frame, Handle parent) {
    const uint16_t i = alloc_node();
    if (i == kNone) {
        release_data(kind, data);
        return {};
    }

    Node& n = nodes_[i];
    n.kind  = kind;
    n.data  = data;
    n.frame = frame;
    if (kind == Kind::Image || kind == Kind::Text) {
        n.flags = with_size_mode(n.flags, Axis::Horizontal, SizeMode::Fit);
        n.flags = with_size_mode(n.flags, Axis::Vertical, SizeMode::Fit);
    }

    const Handle h = handle_of(i);
    if (parent && !attach(h, parent)) {
        release_node(i);
        return {};
    }
    return h;
}

Handle Context::create_root(float width, float height) {
    const uint16_t i = alloc_node();
    if (i == kNone)
        return {};

    Node& n        = nodes_[i];
    n.flags        |= flag::root;
    n.frame.width  = width;
    n.frame.height = height;

    const Handle h = handle_of(i);
    roots_.push_back(h);
    return h;
}

Handle Context::create_group(Handle parent, const Frame& frame) {
    return spawn(Kind::Group, kNone, frame, parent);
}

Handle Context::create_rect(Handle parent, const RectDesc& d) {
    const uint16_t data = rects_.acquire(RectData{d.color, d.radius});
    if (data == kNone)
        return {};
    return spawn(Kind::Rect, data, d.frame, parent);
}

Handle Context::create_image(Handle parent, ImageDesc d) {
    const uint16_t data = images_.acquire(ImageData{move(d.texture), d.radius});
    if (data == kNone)
        return {};
    return spawn(Kind::Image, data, d.frame, parent);
}

Handle Context::create_text(Handle parent, TextDesc d) {
    const uint16_t data = texts_.acquire(TextData{move(d.texture), d.font, d.color});
    if (data == kNone)
        return {};
    return spawn(Kind::Text, data, d.frame, parent);
}

void Context::destroy(Handle h) {
    Node* n = node(h);
    if (!n)
        return;

    const uint16_t root = h.id;

    if (n->flags & flag::root) {
        erase(roots_, h);
    }
    else if (n->parent != kNone) {
        const uint16_t old_parent = unlink(root);
        mark_dirty(old_parent);
    }

    uint16_t cur = root;
    for (;;) {
        Node& c = nodes_[cur];
        if (c.first != kNone) {
            cur = c.first;
            continue;
        }

        const uint16_t up = (cur == root) ? kNone : c.parent;
        if (up != kNone)
            nodes_[up].first = c.next;

        release_node(cur);

        if (up == kNone)
            break;
        cur = up;
    }

}

void Context::clear() {
    while (!roots_.empty())
        destroy(roots_.back());
}

void Context::link_last(uint16_t parent, uint16_t child) {
    Node& p = nodes_[parent];
    Node& c = nodes_[child];

    c.parent = parent;
    c.next   = kNone;
    c.prev   = p.last;
    if (p.last != kNone)
        nodes_[p.last].next = child;
    else
        p.first = child;
    p.last = child;
}

void Context::link_first(uint16_t parent, uint16_t child) {
    Node& p = nodes_[parent];
    Node& c = nodes_[child];

    c.parent = parent;
    c.prev   = kNone;
    c.next   = p.first;
    if (p.first != kNone)
        nodes_[p.first].prev = child;
    else
        p.last = child;
    p.first = child;
}

uint16_t Context::unlink(uint16_t child) {
    Node&          c      = nodes_[child];
    const uint16_t parent = c.parent;
    if (parent == kNone)
        return kNone;

    Node& p = nodes_[parent];
    if (c.prev != kNone)
        nodes_[c.prev].next = c.next;
    else
        p.first = c.next;

    if (c.next != kNone)
        nodes_[c.next].prev = c.prev;
    else
        p.last = c.prev;

    c.parent = c.prev = c.next = kNone;
    return parent;
}

void Context::mark_dirty(uint16_t i) {
    while (i != kNone) {
        Node& n = nodes_[i];
        if (n.flags & flag::dirty)
            return;
        n.flags |= flag::dirty;
        i       = n.parent;
    }
}

bool Context::attach(Handle child, Handle parent) {
    Node* c = node(child);
    Node* p = node(parent);
    if (!c || !p || child == parent || (c->flags & flag::root))
        return false;

    for (uint16_t a = parent.id; a != kNone; a = nodes_[a].parent)
        if (a == child.id)
            return false;

    if (c->parent != kNone)
        mark_dirty(unlink(child.id));

    link_last(parent.id, child.id);
    mark_dirty(parent.id);
    return true;
}

bool Context::detach(Handle h) {
    Node* n = node(h);
    if (!n || n->parent == kNone)
        return false;
    mark_dirty(unlink(h.id));
    return true;
}

void Context::move_to_front(Handle h) {
    Node* n = node(h);
    if (!n)
        return;

    if (n->flags & flag::root) {
        if (const auto it = find(roots_.begin(), roots_.end(), h); it != roots_.end())
            rotate(it, it + 1, roots_.end());
        return;
    }
    if (n->parent == kNone || n->next == kNone)
        return;

    const uint16_t p = unlink(h.id);
    link_last(p, h.id);
    if (nodes_[p].layout != kNone)
        mark_dirty(p);
}

void Context::move_to_back(Handle h) {
    Node* n = node(h);
    if (!n)
        return;

    if (n->flags & flag::root) {
        if (const auto it = find(roots_.begin(), roots_.end(), h); it != roots_.end())
            rotate(roots_.begin(), it, it + 1);
        return;
    }
    if (n->parent == kNone || n->prev == kNone)
        return;

    const uint16_t p = unlink(h.id);
    link_first(p, h.id);
    if (nodes_[p].layout != kNone)
        mark_dirty(p);
}

Handle Context::parent(Handle h) const {
    const Node* n = node(h);
    return n && n->parent != kNone ? handle_of(n->parent) : Handle{};
}

Context::ChildRange Context::children(Handle h) const {
    const Node* n = node(h);
    return {this, n ? n->first : kNone};
}

void Context::set_pos(Handle h, float x, float y) {
    if (Node* n = node(h)) {
        n->frame.x = x;
        n->frame.y = y;
    }
}

void Context::set_size(Handle h, float w, float hgt) {
    if (Node* n = node(h); n && (n->frame.width != w || n->frame.height != hgt)) {
        n->frame.width  = w;
        n->frame.height = hgt;
        mark_dirty(h.id);
    }
}

void Context::set_size_mode(Handle h, Axis axis, SizeMode mode) {
    if (Node* n = node(h); n && size_mode(n->flags, axis) != mode) {
        n->flags = with_size_mode(n->flags, axis, mode);
        mark_dirty(h.id);
    }
}

void Context::set_grow(Handle h, Axis axis, const GrowParams& params) {
    Node* n = node(h);
    if (!n)
        return;

    GrowParams p                           = params;
    p.weight                               = max(p.weight, 0.f);
    p.min                                  = max(p.min, 0.f);
    p.max                                  = max(p.max, p.min);
    GrowData& d                            = grow_[h.id];
    (axis == Axis::Horizontal ? d.h : d.v) = p;
    n->flags                               = with_size_mode(n->flags, axis, SizeMode::Grow);
    mark_dirty(h.id);
}

void Context::set_flag(Handle h, uint16_t bit, bool on) {
    if (Node* n = node(h))
        n->flags = static_cast<uint16_t>(on ? (n->flags | bit) : (n->flags & ~bit));
}

void Context::set_visible(Handle h, bool v) {
    if (Node* n = node(h); n && ((n->flags & flag::visible) != 0) != v) {
        set_flag(h, flag::visible, v);
        mark_dirty(h.id);
    }
}

void Context::set_opacity(Handle h, float v) {
    if (Node* n = node(h))
        n->opacity = quantize_opacity(v);
}

void Context::set_clip(Handle h, bool v) { set_flag(h, flag::clip, v); }
void Context::set_clickable(Handle h, bool v) { set_flag(h, flag::clickable, v); }
void Context::set_hoverable(Handle h, bool v) { set_flag(h, flag::hoverable, v); }
void Context::set_draggable(Handle h, bool v) { set_flag(h, flag::draggable, v); }

void Context::set_tag(Handle h, uint32_t tag) {
    if (Node* n = node(h))
        n->tag = tag;
}

void Context::set_layout(Handle h, const LayoutData& layout) {
    Node* n = node(h);
    if (!n)
        return;

    if (n->layout == kNone) {
        const uint16_t idx = layouts_.acquire(layout);
        if (idx == kNone)
            return;
        n->layout = idx;
    }
    else {
        layouts_[n->layout] = layout;
    }
    mark_dirty(h.id);
}

void Context::clear_layout(Handle h) {
    if (Node* n = node(h); n && n->layout != kNone) {
        layouts_.release(n->layout);
        n->layout = kNone;
        mark_dirty(h.id);
    }
}

Frame Context::frame(Handle h) const {
    const Node* n = node(h);
    return n ? n->frame : Frame{};
}

Frame Context::absolute_frame(Handle h) const {
    const Node* n = node(h);
    if (!n)
        return {};

    Frame f = n->frame;
    for (uint16_t p = n->parent; p != kNone; p = nodes_[p].parent) {
        f.x += nodes_[p].frame.x;
        f.y += nodes_[p].frame.y;
    }
    return f;
}

uint32_t Context::tag(Handle h) const {
    const Node* n = node(h);
    return n ? n->tag : 0;
}

float Context::radius_of(const Node& n) const {
    switch (n.kind) {
    case Kind::Rect:
        return rects_[n.data].radius;
    case Kind::Image:
        return images_[n.data].radius;
    default:
        return 0.f;
    }
}

optional<float> Context::get_prop(Handle h, Prop p) const {
    const Node* n = node(h);
    if (!n)
        return nullopt;

    switch (p) {
    case Prop::X:
        return n->frame.x;
    case Prop::Y:
        return n->frame.y;
    case Prop::W:
        return n->frame.width;
    case Prop::H:
        return n->frame.height;
    case Prop::Opacity:
        return static_cast<float>(n->opacity) * (1.f / 255.f);
    case Prop::Radius:
        if (n->kind == Kind::Rect || n->kind == Kind::Image)
            return radius_of(*n);
        return nullopt;
    case Prop::ColorR:
    case Prop::ColorG:
    case Prop::ColorB:
    case Prop::ColorA: {
        const unsigned ch = static_cast<unsigned>(p) - static_cast<unsigned>(Prop::ColorR);
        if (n->kind == Kind::Rect)
            return color_channel(rects_[n->data].color, ch);
        if (n->kind == Kind::Text)
            return color_channel(texts_[n->data].color, ch);
        return nullopt;
    }
    }
    return nullopt;
}

bool Context::set_prop(Handle h, Prop p, float v) {
    Node* n = node(h);
    if (!n)
        return false;

    switch (p) {
    case Prop::X:
        n->frame.x = v;
        return true;
    case Prop::Y:
        n->frame.y = v;
        return true;
    case Prop::W:
        v = max(v, 0.f);
        if (n->frame.width != v) {
            n->frame.width = v;
            mark_dirty(h.id);
        }
        return true;
    case Prop::H:
        v = max(v, 0.f);
        if (n->frame.height != v) {
            n->frame.height = v;
            mark_dirty(h.id);
        }
        return true;
    case Prop::Opacity:
        n->opacity = quantize_opacity(v);
        return true;
    case Prop::Radius:
        if (n->kind == Kind::Rect) {
            rects_[n->data].radius = max(v, 0.f);
            return true;
        }
        if (n->kind == Kind::Image) {
            images_[n->data].radius = max(v, 0.f);
            return true;
        }
        return false;
    case Prop::ColorR:
    case Prop::ColorG:
    case Prop::ColorB:
    case Prop::ColorA: {
        const unsigned ch = static_cast<unsigned>(p) - static_cast<unsigned>(Prop::ColorR);
        if (n->kind == Kind::Rect) {
            color_channel(rects_[n->data].color, ch) = v;
            return true;
        }
        if (n->kind == Kind::Text) {
            color_channel(texts_[n->data].color, ch) = v;
            return true;
        }
        return false;
    }
    }
    return false;
}

RectView Context::rect(Handle h) {
    const Node* n = node(h);
    return n && n->kind == Kind::Rect ? RectView{this, h} : RectView{};
}

ImageView Context::image(Handle h) {
    const Node* n = node(h);
    return n && n->kind == Kind::Image ? ImageView{this, h} : ImageView{};
}

TextView Context::text(Handle h) {
    const Node* n = node(h);
    return n && n->kind == Kind::Text ? TextView{this, h} : TextView{};
}

bool Context::animate(SAnimDesc desc) {
    return anim_.start(*this, desc.h, desc.p, desc.to, desc.s, desc.delay);
}

bool Context::animate(TAnimDesc desc) {
    return anim_.start(*this, desc.h, desc.p, desc.to, desc.t, desc.delay);
}

void Context::update(float dt) {
    const bool animated = anim_.update(*this, dt);
    refresh_text(Renderer::text_scale());
    const bool laid_out = layout();
    if (animated || laid_out)
        update_hover();
}

bool Context::refresh_text(float scale) {
    if (!(scale > 0.0f))
        return false;

    bool changed{};
    for (uint16_t i{}; i < nodes_.size(); ++i) {
        Node& n = nodes_[i];
        if (n.kind != Kind::Text)
            continue;

        TextData& data = texts_[n.data];
        if (data.text.empty() || !data.texture.valid() || data.texture.scale() == scale)
            continue;

        data.texture = Renderer::prepare_text(data.text, data.font);
        mark_dirty(i);
        changed = true;
    }
    return changed;
}

ImageView& ImageView::set_image(std::string_view path) {
    if (auto* d = data()) {
        d->texture = Renderer::prepare_image(path);
        ctx_->mark_dirty(h_.id);
    }
    return *this;
}

TextView& TextView::set_text(std::string_view s) {
    if (auto* d = data()) {
        d->text     = s;
        d->texture  = Renderer::prepare_text(s, d->font);
        ctx_->mark_dirty(h_.id);
    }
    return *this;
}

}