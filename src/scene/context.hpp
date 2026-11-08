#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

#include "animation.hpp"
#include "struct.hpp"

namespace Object {
struct RectDesc {
    Frame frame{};
    Color color{};
    float radius{};
};

struct ImageDesc {
    Frame   frame{};
    Texture texture{};
    float   radius{};
};

struct TextDesc {
    Frame      frame{};
    Texture    texture{};
    FontHandle font{};
    Color      color{};
};

struct SAnimDesc {
    Handle        h;
    float         to{};
    const Spring& s;
    float         delay{};
    Prop          p;
};

struct TAnimDesc {
    Handle        h;
    float         to{};
    const Timing& t;
    float         delay{};
    Prop          p;
};

template <class R>
concept Renderer = requires(R& r, const Frame& f, const Color& c, const Texture& t,
    const FontHandle&          font, float     x)
    {
        r.draw_rect(f, c, x /*radius*/, x /*opacity*/);
        r.draw_image(f, t, x /*radius*/, x /*opacity*/);
        r.draw_text(f, t, font, c, x /*opacity*/);
        r.push_clip(f, x /*radius*/);
        r.pop_clip();
    };

class Context;

class RectView {
public:
    RectView() = default;

    [[nodiscard]] explicit operator bool() const;

    RectView& set_color(const Color& c);

    RectView& set_radius(float r);

    [[nodiscard]] Color color() const;

    [[nodiscard]] float radius() const;

private:
    friend class Context;

    RectView(Context* c, Handle h) : ctx_(c), h_(h) {
    }

    [[nodiscard]] RectData* data() const;

    Context* ctx_ = nullptr;
    Handle   h_{};
};

class ImageView {
public:
    ImageView() = default;

    [[nodiscard]] explicit operator bool() const;

    ImageView& set_texture(Texture t); // may change the Fit size -> re-layout
    ImageView& set_radius(float r);

    [[nodiscard]] float radius() const;

private:
    friend class Context;

    ImageView(Context* c, Handle h) : ctx_(c), h_(h) {
    }

    [[nodiscard]] ImageData* data() const;

    Context* ctx_ = nullptr;
    Handle   h_{};
};

class TextView {
public:
    TextView() = default;

    [[nodiscard]] explicit operator bool() const;

    TextView& set_texture(Texture t); // text is rendered to a texture by the caller
    TextView& set_font(FontHandle f);

    TextView& set_color(const Color& c);

    [[nodiscard]] Color color() const;

private:
    friend class Context;

    TextView(Context* c, Handle h) : ctx_(c), h_(h) {
    }

    [[nodiscard]] TextData* data() const;

    Context* ctx_ = nullptr;
    Handle   h_{};
};

class Context {
public:
    explicit Context(std::size_t reserve_nodes = 128);

    Context(const Context&) = delete;

    Context& operator=(const Context&) = delete;

    Context(Context&&) = default;

    Context& operator=(Context&&) = default;

    Handle create_root(float width, float height);

    Handle create_group(Handle parent = {}, const Frame& frame = {});

    Handle create_rect(Handle parent = {}, const RectDesc& desc = {});

    Handle create_image(Handle parent = {}, const ImageDesc& desc = {});

    Handle create_text(Handle parent = {}, const TextDesc& desc = {});

    void                      destroy(Handle h); // whole subtree; stale handles become invalid
    [[nodiscard]] bool        alive(Handle h) const { return node(h) != nullptr; }
    [[nodiscard]] std::size_t node_count() const { return live_; }

    bool attach(Handle child, Handle parent); // appended on top; re-parents if needed
    bool detach(Handle h);

    void move_to_front(Handle h); // z-order
    void move_to_back(Handle h);

    [[nodiscard]] Handle parent(Handle h) const;

    class ChildRange {
    public:
        class iterator {
        public:
            Handle operator*() const { return ctx_->handle_of(i_); }

            iterator& operator++() {
                i_ = ctx_->nodes_[i_].next;
                return *this;
            }

            bool operator==(const iterator&) const = default;

        private:
            friend class ChildRange;

            iterator(const Context* c, uint16_t i) : ctx_(c), i_(i) {
            }

            const Context* ctx_;
            uint16_t       i_;
        };

        [[nodiscard]] iterator begin() const { return {ctx_, first_}; }
        [[nodiscard]] iterator end() const { return {ctx_, kNone}; }

    private:
        friend class Context;

        ChildRange(const Context* c, uint16_t f) : ctx_(c), first_(f) {
        }

        const Context* ctx_;
        uint16_t       first_;
    };

    // Back-most to front-most. Do not create / destroy / re-parent while iterating.
    [[nodiscard]] ChildRange children(Handle h) const;

    void set_pos(Handle h, float x, float y);

    void set_size(Handle h, float w, float hgt);

    void set_size_mode(Handle h, Axis axis, SizeMode mode);

    void set_grow(Handle h, Axis axis, const GrowParams& params = {});

    void set_visible(Handle h, bool v);

    void set_opacity(Handle h, float v);

    void set_clip(Handle h, bool v);

    void set_tag(Handle h, uint32_t tag);

    void set_clickable(Handle h, bool v);

    void set_hoverable(Handle h, bool v);

    void set_draggable(Handle h, bool v);

    void set_layout(Handle h, const LayoutData& layout);

    void clear_layout(Handle h);

    [[nodiscard]] Frame frame(Handle h) const;

    [[nodiscard]] Frame absolute_frame(Handle h) const;

    [[nodiscard]] uint32_t tag(Handle h) const;

    [[nodiscard]] std::optional<float> get_prop(Handle h, Prop p) const;

    bool set_prop(Handle h, Prop p, float value);

    // Typed views; empty (false) if the handle is dead or of another Kind.
    [[nodiscard]] RectView rect(Handle h);

    [[nodiscard]] ImageView image(Handle h);

    [[nodiscard]] TextView text(Handle h);

    bool animate(SAnimDesc desc);

    bool animate(TAnimDesc desc);

    void               cancel_animation(Handle h, Prop p) { anim_.cancel(h, p); }
    [[nodiscard]] bool is_animating(Handle h, Prop p) const { return anim_.active(h, p); }

    void update(float dt);

    bool layout();

    template <Renderer R>
    void draw(R& renderer) const;

    void pointer_move(float x, float y);

    void pointer_button(Button b, bool down);

    void                 pointer_cancel();                 // window focus lost etc.
    [[nodiscard]] bool   poll_event(Event& out);           // call until false, every frame
    [[nodiscard]] Handle hit_test(float x, float y) const; // top-most interactive node
    [[nodiscard]] Handle hovered() const { return hovered_; }
    void                 set_drag_threshold(float px) { drag_threshold_ = px; }

private:
    friend class RectView;
    friend class ImageView;
    friend class TextView;

    // ---- storage -------------------------------------------------------------
    [[nodiscard]] Node* node(Handle h) {
        return h.id < nodes_.size() && nodes_[h.id].gen == h.gen ? &nodes_[h.id] : nullptr;
    }

    [[nodiscard]] const Node* node(Handle h) const {
        return h.id < nodes_.size() && nodes_[h.id].gen == h.gen ? &nodes_[h.id] : nullptr;
    }

    [[nodiscard]] Handle handle_of(uint16_t i) const { return {i, nodes_[i].gen}; }

    [[nodiscard]] uint16_t alloc_node();

    void release_node(uint16_t i);

    void release_data(Kind kind, uint16_t data);

    [[nodiscard]] Handle spawn(Kind kind, uint16_t data, const Frame& frame, Handle parent);

    // ---- tree helpers ----------------------------------------------------------
    void link_last(uint16_t parent, uint16_t child);

    void link_first(uint16_t parent, uint16_t child);

    uint16_t unlink(uint16_t child); // returns the old parent
    void     mark_dirty(uint16_t i);

    void set_flag(Handle h, uint16_t bit, bool on);

    void measure(uint16_t i);

    void arrange(uint16_t i);

    void arrange_children(Node& n);

    float resolve_grow(Node& n, Axis axis, float space);

    [[nodiscard]] GrowParams grow_params(uint16_t id, Axis axis) const;

    [[nodiscard]] float radius_of(const Node& n) const;

    template <class R>
    void draw_node(R& r, uint16_t i, float ox, float oy, float alpha) const;

    [[nodiscard]] uint16_t hit_node(uint16_t i, float x, float y, float ox, float oy) const;

    void update_hover();

    void validate_input();

    void emit(Handle target, EventType type, Button b, float dx, float dy);

    std::vector<Node>   nodes_;
    uint16_t            free_head_ = kNone;
    std::size_t         live_      = 0;
    std::vector<Handle> roots_; // back-to-front

    SlotVec<RectData>   rects_;
    SlotVec<ImageData>  images_;
    SlotVec<TextData>   texts_;
    SlotVec<LayoutData> layouts_;
    std::unordered_map<uint16_t, GrowData> grow_;

    Animator anim_;

    std::vector<Event> events_;
    std::size_t        event_head_ = 0;
    Handle             hovered_{};
    Handle             pressed_{};
    Button             pressed_button_ = Button::None;
    float              px_             = 0.f, py_      = 0.f;
    float              press_x_        = 0.f, press_y_ = 0.f;
    float              drag_threshold_ = 4.f;
    bool               has_pointer_    = false;
    bool               dragging_       = false;
};

inline RectData* RectView::data() const {
    Node* n = ctx_ ? ctx_->node(h_) : nullptr;
    return n ? &ctx_->rects_[n->data] : nullptr;
}

inline ImageData* ImageView::data() const {
    Node* n = ctx_ ? ctx_->node(h_) : nullptr;
    return n ? &ctx_->images_[n->data] : nullptr;
}

inline TextData* TextView::data() const {
    Node* n = ctx_ ? ctx_->node(h_) : nullptr;
    return n ? &ctx_->texts_[n->data] : nullptr;
}

inline RectView::operator bool() const { return ctx_ && ctx_->alive(h_); }
inline ImageView::operator bool() const { return ctx_ && ctx_->alive(h_); }
inline TextView::operator bool() const { return ctx_ && ctx_->alive(h_); }

inline RectView& RectView::set_color(const Color& c) {
    if (auto* d = data())
        d->color = c;
    return *this;
}

inline RectView& RectView::set_radius(float r) {
    if (auto* d = data())
        d->radius = r < 0.f ? 0.f : r;
    return *this;
}

inline Color RectView::color() const {
    auto* d = data();
    return d ? d->color : Color{};
}

inline float RectView::radius() const {
    auto* d = data();
    return d ? d->radius : 0.f;
}

inline ImageView& ImageView::set_texture(Texture t) {
    if (auto* d = data()) {
        d->texture = std::move(t);
        ctx_->mark_dirty(h_.id);
    }
    return *this;
}

inline ImageView& ImageView::set_radius(float r) {
    if (auto* d = data())
        d->radius = r < 0.f ? 0.f : r;
    return *this;
}

inline float ImageView::radius() const {
    auto* d = data();
    return d ? d->radius : 0.f;
}

inline TextView& TextView::set_texture(Texture t) {
    if (auto* d = data()) {
        d->texture = std::move(t);
        ctx_->mark_dirty(h_.id);
    }
    return *this;
}

inline TextView& TextView::set_font(FontHandle f) {
    if (auto* d = data())
        d->font = f;
    return *this;
}

inline TextView& TextView::set_color(const Color& c) {
    if (auto* d = data())
        d->color = c;
    return *this;
}

inline Color TextView::color() const {
    auto* d = data();
    return d ? d->color : Color{};
}
}