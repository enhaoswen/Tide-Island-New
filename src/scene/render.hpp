#pragma once

#include "context.hpp"

namespace Object {

template <class R>
concept Renderer = requires(R& r, const Frame& f, const Color& c, const Texture& t,
    const FontHandle&          font, float     x, const UvRect& uv)
    {
        r.draw_rect(f, c, x /*radius*/, x /*opacity*/);
        r.draw_image(f, t, uv, x /*radius*/, x /*opacity*/);
        r.draw_text(f, t, font, c, x /*opacity*/);
        r.draw_ring(f, c, c /*track*/, x /*progress*/, x /*thickness*/, x /*opacity*/);
        r.push_clip(f, x /*radius*/);
        r.pop_clip();
    };

struct Drawer {
    template <class R>
    static void node(Context& c, R& r, uint16_t i, const Placed& parent, float alpha) {
        const Node& n = c.nodes_[i];
        if (!(n.flags & flag::visible))
            return;

        alpha *= static_cast<float>(n.opacity) * (1.f / 255.f);
        if (alpha <= 0.f)
            return;

        const Placed p   = c.place(n, parent);
        const Frame& abs = p.frame;

        float radius = 0.f;
        switch (n.kind) {
        case Kind::Group:
            break;
        case Kind::Rect: {
            const RectData& d = c.rects_[n.data];
            radius            = d.radius * p.scale;
            r.draw_rect(abs, d.color, radius, alpha);
            break;
        }
        case Kind::Image: {
            const ImageData& d = c.images_[n.data];
            radius             = d.radius * p.scale;
            r.draw_image(abs, d.texture, d.uv, radius, alpha);
            break;
        }
        case Kind::Text: {
            TextData&   d = c.texts_[n.data];
            const Frame f = c.prepare_text_draw(d, abs);
            r.draw_text(f, d.texture, d.font, d.color, alpha);
            break;
        }
        case Kind::Ring: {
            const RingData& d = c.rings_[n.data];
            r.draw_ring(abs, d.color, d.track, d.progress, d.thickness * p.scale, alpha);
            break;
        }
        }

        const bool clip = (n.flags & flag::clip) != 0;
        if (clip)
            r.push_clip(abs, radius);

        for (uint16_t ch = n.first; ch != k_none; ch = c.nodes_[ch].next)
            node(c, r, ch, p, alpha);

        if (clip)
            r.pop_clip();
    }

    template <class R>
    static void all(Context& c, R& r) {
        for (const Handle root : c.roots_)
            node(c, r, root.id, Placed{}, 1.f);
    }
};

template <Renderer R>
void draw(Context& ctx, R& renderer) {
    Drawer::all(ctx, renderer);
}

}