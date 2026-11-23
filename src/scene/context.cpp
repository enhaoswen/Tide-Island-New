#include "context.hpp"

#include "graphics/renderer.hpp"
#include "graphics/text.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace std;

namespace Object {

namespace {
[[nodiscard]] uint8_t quantize_opacity(float v) {
    v = clamp(v, 0.f, 1.f);
    return static_cast<uint8_t>(v * 255.f + 0.5f);
}

} // namespace

Context::Context(size_t reserve_nodes) {
    nodes_.reserve(min<size_t>(reserve_nodes, k_none));
}

uint16_t Context::alloc_node() {
    uint16_t i;
    if (free_head_ != k_none) {
        i = free_head_;
        free_head_ = nodes_[i].next;
    }
    else {
        if (nodes_.size() >= k_none)
            return k_none;
        i = static_cast<uint16_t>(nodes_.size());
        nodes_.emplace_back();
    }

    Node& n = nodes_[i];
    const uint16_t gen = n.gen;
    n = Node{};
    n.gen = gen;
    ++live_;
    return i;
}

void Context::release_data(Kind kind, uint16_t data) {
    if (data == k_none)
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
    case Kind::Ring:
        rings_.release(data);
        break;
    }
}

void Context::release_node(uint16_t i) {
    Node& n = nodes_[i];
    release_data(n.kind, n.data);
    if (n.layout != k_none)
        layouts_.release(n.layout);

    unbind(n, EventMask::all());

    if (n.transform != k_none)
        transforms_.release(n.transform);

    if (!grow_.empty())
        grow_.erase(i);

    const auto next_gen = static_cast<uint16_t>(n.gen + 1);

    n = Node{};
    n.gen = next_gen;
    n.next = free_head_;
    free_head_ = i;
    --live_;
}

Handle Context::spawn(Kind kind, uint16_t data, const Frame& frame, Handle parent) {
    const uint16_t i = alloc_node();
    if (i == k_none) {
        release_data(kind, data);
        return {};
    }

    Node& n = nodes_[i];
    n.kind = kind;
    n.data = data;
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
    if (i == k_none)
        return {};

    Node& n = nodes_[i];
    n.flags |= flag::root;
    n.frame.width = width;
    n.frame.height = height;

    const Handle h = handle_of(i);
    roots_.push_back(h);
    return h;
}

Handle Context::create_group(Handle parent, const Frame& frame) {
    return spawn(Kind::Group, k_none, frame, parent);
}

Handle Context::create_rect(Handle parent, const RectDesc& d) {
    const uint16_t data = rects_.acquire(RectData{d.color, d.radius});
    if (data == k_none)
        return {};
    return spawn(Kind::Rect, data, d.frame, parent);
}

Handle Context::create_image(Handle parent, ImageDesc d) {
    const uint16_t data = images_.acquire(ImageData{std::move(d.texture), d.radius});
    if (data == k_none)
        return {};
    return spawn(Kind::Image, data, d.frame, parent);
}

Handle Context::create_text(Handle parent, TextDesc d) {
    TextData td{};
    td.font  = d.font;
    td.color = d.color;
    td.set_content(std::move(d.texture));
    const uint16_t data = texts_.acquire(std::move(td));
    if (data == k_none)
        return {};
    return spawn(Kind::Text, data, d.frame, parent);
}

Handle Context::create_ring(Handle parent, const RingDesc& d) {
    Color track = d.track.value_or(d.color);
    if (!d.track)
        color_channel(track, 3) *= .25f;
    const uint16_t data = rings_.acquire(RingData{d.color, track, clamp(d.progress, 0.f, 1.f), max(d.thickness, 0.f)});
    if (data == k_none)
        return {};
    return spawn(Kind::Ring, data, d.frame, parent);
}

void Context::destroy(Handle h) {
    Node* n = node(h);
    if (!n)
        return;

    const uint16_t root = h.id;

    if (n->flags & flag::root) {
        erase(roots_, h);
    }
    else if (n->parent != k_none) {
        const uint16_t old_parent = unlink(root);
        mark_dirty(old_parent);
    }

    uint16_t cur = root;
    for (;;) {
        Node& c = nodes_[cur];
        if (c.first != k_none) {
            cur = c.first;
            continue;
        }

        const uint16_t up = (cur == root) ? k_none : c.parent;
        if (up != k_none)
            nodes_[up].first = c.next;

        release_node(cur);

        if (up == k_none)
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
    c.next = k_none;
    c.prev = p.last;
    if (p.last != k_none)
        nodes_[p.last].next = child;
    else
        p.first = child;
    p.last = child;
}

void Context::link_first(uint16_t parent, uint16_t child) {
    Node& p = nodes_[parent];
    Node& c = nodes_[child];

    c.parent = parent;
    c.prev = k_none;
    c.next = p.first;
    if (p.first != k_none)
        nodes_[p.first].prev = child;
    else
        p.last = child;
    p.first = child;
}

uint16_t Context::unlink(uint16_t child) {
    Node& c = nodes_[child];
    const uint16_t parent = c.parent;
    if (parent == k_none)
        return k_none;

    Node& p = nodes_[parent];
    if (c.prev != k_none)
        nodes_[c.prev].next = c.next;
    else
        p.first = c.next;

    if (c.next != k_none)
        nodes_[c.next].prev = c.prev;
    else
        p.last = c.prev;

    c.parent = c.prev = c.next = k_none;
    return parent;
}

void Context::mark_dirty(uint16_t i) {
    while (i != k_none) {
        Node& n = nodes_[i];
        if (n.flags & flag::dirty)
            return;
        n.flags |= flag::dirty;
        i = n.parent;
    }
}

bool Context::attach(Handle child, Handle parent) {
    Node* c = node(child);
    Node* p = node(parent);
    if (!c || !p || child == parent || (c->flags & flag::root))
        return false;

    for (uint16_t a = parent.id; a != k_none; a = nodes_[a].parent)
        if (a == child.id)
            return false;

    if (c->parent != k_none)
        mark_dirty(unlink(child.id));

    link_last(parent.id, child.id);
    mark_dirty(parent.id);
    return true;
}

bool Context::detach(Handle h) {
    Node* n = node(h);
    if (!n || n->parent == k_none)
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
    if (n->parent == k_none || n->next == k_none)
        return;

    const uint16_t p = unlink(h.id);
    link_last(p, h.id);
    if (nodes_[p].layout != k_none)
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
    if (n->parent == k_none || n->prev == k_none)
        return;

    const uint16_t p = unlink(h.id);
    link_first(p, h.id);
    if (nodes_[p].layout != k_none)
        mark_dirty(p);
}

Handle Context::parent(Handle h) const {
    const Node* n = node(h);
    return n && n->parent != k_none ? handle_of(n->parent) : Handle{};
}

Context::ChildRange Context::children(Handle h) const {
    const Node* n = node(h);
    return {this, n ? n->first : k_none};
}

void Context::set_pos(Handle h, float x, float y) {
    if (Node* n = node(h)) {
        n->frame.x = x;
        n->frame.y = y;
    }
}

void Context::set_size(Handle h, float w, float hgt) {
    if (Node* n = node(h); n && (n->frame.width != w || n->frame.height != hgt)) {
        n->frame.width = w;
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

    GrowParams p = params;
    p.weight = max(p.weight, 0.f);
    p.min = max(p.min, 0.f);
    p.max = max(p.max, p.min);
    GrowData& d = grow_[h.id];
    (axis == Axis::Horizontal ? d.h : d.v) = p;
    n->flags = with_size_mode(n->flags, axis, SizeMode::Grow);
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

    if (n->layout == k_none) {
        const uint16_t idx = layouts_.acquire(layout);
        if (idx == k_none)
            return;
        n->layout = idx;
    }
    else {
        layouts_[n->layout] = layout;
    }
    mark_dirty(h.id);
}

void Context::clear_layout(Handle h) {
    if (Node* n = node(h); n && n->layout != k_none) {
        layouts_.release(n->layout);
        n->layout = k_none;
        mark_dirty(h.id);
    }
}

Frame Context::frame(Handle h) const {
    const Node* n = node(h);
    return n ? n->frame : Frame{};
}

Frame Context::absolute_frame(Handle h) const {
    return node(h) ? placed(h.id).frame : Frame{};
}

Placed Context::placed(uint16_t i) const {
    const Node& n = nodes_[i];
    return place(n, n.parent == k_none ? Placed{} : placed(n.parent));
}

Transform& Context::transform_of(Node& n) {
    if (n.transform == k_none)
        n.transform = transforms_.acquire({});
    return transforms_[n.transform];
}

void Context::set_scale(Handle h, float s) {
    set_prop(h, Prop::Scale, s);
}

void Context::set_pivot(Handle h, float x, float y) {
    if (Node* n = node(h)) {
        Transform& t = transform_of(*n);
        t.pivot_x    = x;
        t.pivot_y    = y;
    }
}

float Context::scale(Handle h) const {
    return get_prop(h, Prop::Scale).value_or(1.f);
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
    case Prop::Scale:
        return n->transform == k_none ? 1.f : transforms_[n->transform].scale;
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
        if (n->kind == Kind::Ring)
            return color_channel(rings_[n->data].color, ch);
        return nullopt;
    }
    case Prop::Progress:
        if (n->kind == Kind::Ring)
            return rings_[n->data].progress;
        return nullopt;
    case Prop::Thickness:
        if (n->kind == Kind::Ring)
            return rings_[n->data].thickness;
        return nullopt;
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
    case Prop::Scale:
        transform_of(*n).scale = max(v, 0.f);
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
        if (n->kind == Kind::Ring) {
            color_channel(rings_[n->data].color, ch) = v;
            return true;
        }
        return false;
    }
    case Prop::Progress:
        if (n->kind != Kind::Ring)
            return false;
        // Springs overshoot; clamping keeps the arc from wrapping past full or vanishing.
        rings_[n->data].progress = clamp(v, 0.f, 1.f);
        return true;
    case Prop::Thickness:
        if (n->kind != Kind::Ring)
            return false;
        rings_[n->data].thickness = max(v, 0.f);
        return true;
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

RingView Context::ring(Handle h) {
    const Node* n = node(h);
    return n && n->kind == Kind::Ring ? RingView{this, h} : RingView{};
}

TextView Context::text(Handle h) {
    const Node* n = node(h);
    return n && n->kind == Kind::Text ? TextView{this, h} : TextView{};
}

bool Context::animate(SAnimDesc desc) {
    return anim_.start(*this, desc.h, desc.p, desc.to, desc.s, desc.delay, std::move(desc.on_done));
}

bool Context::animate(TAnimDesc desc) {
    return anim_.start(*this, desc.h, desc.p, desc.to, desc.t, desc.delay, std::move(desc.on_done));
}

void Context::update(float dt) {
    const bool animated = anim_.update(*this, dt);
    const bool laid_out = layout();
    if (animated || laid_out)
        update_hover();
}

float Context::text_zoom(uint16_t i) const {
    const TextData& d = texts_[nodes_[i].data];
    const Placed    p = placed(i);
    const float     z = d.natural_w > 0.f ? p.frame.width / d.natural_w : p.scale;
    return z > 0.f ? z : 1.f;
}

std::uint64_t Context::raster_key(const TextData& d, float scale, int phase_x, int phase_y) {
    const std::uint64_t size = d.raster ? static_cast<std::uint64_t>(lround(scale * 1024.f)) + 1
                                        : Text::raster_size(d.font, scale);
    return size ? size << 4 | static_cast<std::uint64_t>(phase_x) << 2 | static_cast<std::uint64_t>(phase_y) : 0;
}

Texture Context::raster_text(const TextData& d, float scale, float sub_x, float sub_y) {
    if (d.raster)
        return d.raster(scale, sub_x, sub_y, d.raster_arg);
    if (!d.text.empty())
        return Renderer::prepare_text(d.text, d.font, scale, sub_x, sub_y);
    return {};
}

void Context::rasterize(uint16_t i, TextData& d) {
    const float want = Renderer::text_scale() * text_zoom(i);
    d.set_content(raster_text(d, want, 0.f, 0.f));
    d.raster_key = raster_key(d, want, 0, 0);
}

Frame Context::prepare_text_draw(TextData& d, const Frame& abs) {
    if (!d.texture.valid() || (!d.raster && d.text.empty()))
        return abs;

    const float scale = Renderer::text_scale();
    const float zoom  = d.natural_w > 0.f ? abs.width / d.natural_w : 1.f;
    const float want  = scale * zoom;

    const auto split = [](float v, float& whole) {
        const float quarters = std::round(v * 4.f);
        whole                = std::floor(quarters / 4.f);
        return quarters / 4.f - whole;
    };
    float       whole_x{}, whole_y{};
    const float sub_x = split((abs.x - d.natural_origin_x * zoom) * scale, whole_x);
    const float sub_y = split((abs.y - d.natural_origin_y * zoom) * scale, whole_y);

    const std::uint64_t key = raster_key(d, want, static_cast<int>(sub_x * 4.f), static_cast<int>(sub_y * 4.f));
    if (key != 0 && key != d.raster_key) {
        if (Texture t = raster_text(d, want, sub_x, sub_y); t.valid()) {
            d.texture    = std::move(t);
            d.raster_key = key;
        }
    }

    // The key may keep a raster from a zoom a hair away; still draw it 1:1 instead of blurring.
    const float k = std::abs(d.texture.scale() - want) <= want * 0.01f ? d.texture.scale() / scale : zoom;
    return {
        .x      = whole_x / scale + d.texture.origin_x() * k,
        .y      = whole_y / scale + d.texture.origin_y() * k,
        .width  = d.texture.logical_width() * k,
        .height = d.texture.logical_height() * k,
    };
}

ImageView& ImageView::set_image(std::string_view path) {
    if (auto* d = data()) {
        d->texture = Renderer::prepare_image(path);
        ctx_->mark_dirty(h_.id);
    }
    return *this;
}

TextView& TextView::set_raster(RasterFn fn, std::uintptr_t arg) {
    if (auto* d = data()) {
        d->raster     = fn;
        d->raster_arg = arg;
        d->text.clear();
        ctx_->rasterize(h_.id, *d);
        ctx_->mark_dirty(h_.id);
    }
    return *this;
}

TextView& TextView::set_text(std::string_view s) {
    if (auto* d = data()) {
        d->raster = nullptr;
        d->text   = s;
        ctx_->rasterize(h_.id, *d);
        ctx_->mark_dirty(h_.id);
    }
    return *this;
}

} // namespace Object
