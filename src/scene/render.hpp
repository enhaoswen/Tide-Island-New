#pragma once

#include "context.hpp"

namespace Object {

// One DFS in sibling order == painter's order: z-order, group opacity and
// clipping all fall out of the tree. State carried down: absolute origin,
// accumulated opacity. The clip stack lives in the renderer.
template <class R>
void Context::draw_node(R& r, uint16_t i, float ox, float oy, float alpha) const {
    const Node& n = nodes_[i];
    if (!(n.flags & flag::visible))
        return;

    alpha *= static_cast<float>(n.opacity) * (1.f / 255.f);
    if (alpha <= 0.f)
        return; // fully transparent: the whole subtree is invisible

    Frame abs = n.frame;
    abs.x += ox;
    abs.y += oy;

    float radius = 0.f;
    switch (n.kind) {
    case Kind::Group:
        break;
    case Kind::Rect: {
        const RectData& d = rects_[n.data];
        radius            = d.radius;
        r.draw_rect(abs, d.color, d.radius, alpha);
        break;
    }
    case Kind::Image: {
        const ImageData& d = images_[n.data];
        radius             = d.radius;
        r.draw_image(abs, d.texture, d.radius, alpha);
        break;
    }
    case Kind::Text: {
        const TextData& d = texts_[n.data];
        r.draw_text(abs, d.texture, d.font, d.color, alpha);
        break;
    }
    }

    const bool clip = (n.flags & flag::clip) != 0;
    if (clip)
        r.push_clip(abs, radius);

    for (uint16_t c = n.first; c != kNone; c = nodes_[c].next)
        draw_node(r, c, abs.x, abs.y, alpha);

    if (clip)
        r.pop_clip();
}

template <Renderer R>
void Context::draw(R& renderer) const {
    for (const Handle root : roots_)
        draw_node(renderer, root.id, 0.f, 0.f, 1.f);
}

}