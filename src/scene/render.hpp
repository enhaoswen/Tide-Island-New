#pragma once

#include "context.hpp"

namespace Object {

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

struct Drawer {
    template <class R>
    static void node(const Context& c, R& r, uint16_t i, float ox, float oy, float alpha) {
        const Node& n = c.nodes_[i];
        if (!(n.flags & flag::visible))
            return;

        alpha *= static_cast<float>(n.opacity) * (1.f / 255.f);
        if (alpha <= 0.f)
            return;

        Frame abs = n.frame;
        abs.x     += ox;
        abs.y     += oy;

        float radius = 0.f;
        switch (n.kind) {
        case Kind::Group:
            break;
        case Kind::Rect: {
            const RectData& d = c.rects_[n.data];
            radius            = d.radius;
            r.draw_rect(abs, d.color, d.radius, alpha);
            break;
        }
        case Kind::Image: {
            const ImageData& d = c.images_[n.data];
            radius             = d.radius;
            r.draw_image(abs, d.texture, d.radius, alpha);
            break;
        }
        case Kind::Text: {
            const TextData& d = c.texts_[n.data];
            r.draw_text(abs, d.texture, d.font, d.color, alpha);
            break;
        }
        }

        const bool clip = (n.flags & flag::clip) != 0;
        if (clip)
            r.push_clip(abs, radius);

        for (uint16_t ch = n.first; ch != kNone; ch = c.nodes_[ch].next)
            node(c, r, ch, abs.x, abs.y, alpha);

        if (clip)
            r.pop_clip();
    }

    template <class R>
    static void all(const Context& c, R& r) {
        for (const Handle root : c.roots_)
            node(c, r, root.id, 0.f, 0.f, 1.f);
    }
};

template <Renderer R>
void draw(const Context& ctx, R& renderer) {
    Drawer::all(ctx, renderer);
}

}