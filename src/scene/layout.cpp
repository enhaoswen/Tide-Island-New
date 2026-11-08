#include "context.hpp"

#include <algorithm>

namespace Object {

GrowParams Context::grow_params(uint16_t id, Axis axis) const {
    if (grow_.empty())
        return {};
    const auto it = grow_.find(id);
    if (it == grow_.end())
        return {};
    return axis == Axis::Horizontal ? it->second.h : it->second.v;
}

bool Context::layout() {
    bool did_work = false;
    for (const Handle root : roots_) {
        if (!(nodes_[root.id].flags & flag::dirty))
            continue;
        measure(root.id);
        arrange(root.id);
        did_work = true;
    }
    return did_work;
}

void Context::measure(uint16_t i) {
    Node& n = nodes_[i];
    if (!(n.flags & flag::dirty))
        return;

    for (uint16_t c = n.first; c != kNone; c = nodes_[c].next)
        measure(c);

    const SizeMode wm = size_mode(n.flags, Axis::Horizontal);
    const SizeMode hm = size_mode(n.flags, Axis::Vertical);

    if (n.kind == Kind::Image || n.kind == Kind::Text) {
        const Texture& tex    = n.kind == Kind::Image ? images_[n.data].texture : texts_[n.data].texture;
        const auto     [w, h] = texture_extent(tex);
        if (wm == SizeMode::Fit)
            n.frame.width = w;
        if (hm == SizeMode::Fit)
            n.frame.height = h;
    }

    if (n.layout == kNone || (wm != SizeMode::Fit && hm != SizeMode::Fit))
        return;

    const LayoutData& L          = layouts_[n.layout];
    const bool        row        = L.dir == Direction::Row;
    const Axis        main_axis  = row ? Axis::Horizontal : Axis::Vertical;
    const Axis        cross_axis = row ? Axis::Vertical : Axis::Horizontal;

    const auto contribution = [this](uint16_t c, Axis a) {
        const Node& ch = nodes_[c];
        return size_mode(ch.flags, a) == SizeMode::Grow ? grow_params(c, a).min : extent(ch.frame, a);
    };

    float    main  = 0.f;
    float    cross = 0.f;
    unsigned count = 0;

    for (uint16_t c = n.first; c != kNone; c = nodes_[c].next) {
        if (!(nodes_[c].flags & flag::visible))
            continue;
        ++count;
        main  += contribution(c, main_axis);
        cross = std::max(cross, contribution(c, cross_axis));
    }
    if (count > 1)
        main += L.spacing * static_cast<float>(count - 1);

    const float w = (row ? main : cross) + L.padding.left + L.padding.right;
    const float h = (row ? cross : main) + L.padding.top + L.padding.bottom;
    if (wm == SizeMode::Fit)
        n.frame.width = w;
    if (hm == SizeMode::Fit)
        n.frame.height = h;
}

void Context::arrange(uint16_t i) {
    Node& n = nodes_[i];

    if (n.layout != kNone)
        arrange_children(n);

    for (uint16_t c = n.first; c != kNone; c = nodes_[c].next)
        if (nodes_[c].flags & flag::dirty)
            arrange(c);

    n.flags &= static_cast<uint16_t>(~flag::dirty);
}

float Context::resolve_grow(Node& n, Axis axis, float space) {
    const auto is_grow = [&](const Node& ch) {
        return (ch.flags & flag::visible) && size_mode(ch.flags, axis) == SizeMode::Grow;
    };

    unsigned remaining = 0;
    for (uint16_t c = n.first; c != kNone; c = nodes_[c].next)
        if (is_grow(nodes_[c]))
            ++remaining;

    float frozen_sum = 0.f;
    while (remaining > 0) {
        float total_weight = 0.f;
        for (uint16_t c = n.first; c != kNone; c = nodes_[c].next) {
            const Node& ch = nodes_[c];
            if (is_grow(ch) && !(ch.flags & flag::frozen))
                total_weight += grow_params(c, axis).weight;
        }

        const float avail  = std::max(space - frozen_sum, 0.f);
        const auto  wanted = [&](const GrowParams& p) {
            return total_weight > 0.f ? avail * p.weight / total_weight : 0.f;
        };

        float violation = 0.f;
        for (uint16_t c = n.first; c != kNone; c = nodes_[c].next) {
            const Node& ch = nodes_[c];
            if (!is_grow(ch) || (ch.flags & flag::frozen))
                continue;
            const GrowParams p = grow_params(c, axis);
            const float      w = wanted(p);
            violation          += std::clamp(w, p.min, p.max) - w;
        }

        for (uint16_t c = n.first; c != kNone; c = nodes_[c].next) {
            Node& ch = nodes_[c];
            if (!is_grow(ch) || (ch.flags & flag::frozen))
                continue;

            const GrowParams p       = grow_params(c, axis);
            const float      w       = wanted(p);
            const float      clamped = std::clamp(w, p.min, p.max);

            const bool freeze = violation == 0.f || (violation > 0.f ? clamped > w : clamped < w);
            if (!freeze)
                continue;

            float& e = extent(ch.frame, axis);
            if (e != clamped) {
                e        = clamped;
                ch.flags |= flag::dirty;
            }
            ch.flags   |= flag::frozen;
            frozen_sum += clamped;
            --remaining;
        }
    }

    for (uint16_t c = n.first; c != kNone; c = nodes_[c].next)
        nodes_[c].flags &= static_cast<uint16_t>(~flag::frozen);

    return frozen_sum;
}

void Context::arrange_children(Node& n) {
    const LayoutData& L          = layouts_[n.layout];
    const bool        row        = L.dir == Direction::Row;
    const Axis        main_axis  = row ? Axis::Horizontal : Axis::Vertical;
    const Axis        cross_axis = row ? Axis::Vertical : Axis::Horizontal;

    const float pad_main_start  = row ? L.padding.left : L.padding.top;
    const float pad_cross_start = row ? L.padding.top : L.padding.left;
    const float pad_main_total  = row ? L.padding.left + L.padding.right : L.padding.top + L.padding.bottom;
    const float pad_cross_total = row ? L.padding.top + L.padding.bottom : L.padding.left + L.padding.right;

    const float avail_main  = std::max(0.f, extent(n.frame, main_axis) - pad_main_total);
    const float avail_cross = std::max(0.f, extent(n.frame, cross_axis) - pad_cross_total);

    float    used  = 0.f;
    unsigned count = 0;
    unsigned grow  = 0;
    for (uint16_t c = n.first; c != kNone; c = nodes_[c].next) {
        const Node& ch = nodes_[c];
        if (!(ch.flags & flag::visible))
            continue;
        ++count;
        if (size_mode(ch.flags, main_axis) == SizeMode::Grow)
            ++grow;
        else
            used += extent(ch.frame, main_axis);
    }
    if (count == 0)
        return;

    const float gaps = L.spacing * static_cast<float>(count - 1);

    const float grown = grow > 0 ? resolve_grow(n, main_axis, avail_main - used - gaps) : 0.f;

    const float leftover = avail_main - used - gaps - grown;
    float       lead     = 0.f;
    float       gap      = L.spacing;
    if (leftover > 1e-3f) {
        switch (L.justify) {
        case Justify::Start:
            break;
        case Justify::Center:
            lead = leftover * .5f;
            break;
        case Justify::End:
            lead = leftover;
            break;
        case Justify::SpaceBetween:
            if (count > 1)
                gap += leftover / static_cast<float>(count - 1);
            break;
        }
    }

    float cursor = pad_main_start + lead;
    for (uint16_t c = n.first; c != kNone; c = nodes_[c].next) {
        Node& ch = nodes_[c];
        if (!(ch.flags & flag::visible))
            continue;

        const float m = extent(ch.frame, main_axis);
        float&      x = extent(ch.frame, cross_axis);

        if (size_mode(ch.flags, cross_axis) == SizeMode::Grow) {
            const GrowParams p = grow_params(c, cross_axis);
            const float      s = std::clamp(avail_cross, p.min, p.max);
            if (x != s) {
                x        = s;
                ch.flags |= flag::dirty;
            }
        }

        float cross_pos = pad_cross_start;
        switch (L.align) {
        case Alignment::Start:
            break;
        case Alignment::Center:
            cross_pos += (avail_cross - x) * .5f;
            break;
        case Alignment::End:
            cross_pos += avail_cross - x;
            break;
        }

        if (row) {
            ch.frame.x = cursor;
            ch.frame.y = cross_pos;
        }
        else {
            ch.frame.y = cursor;
            ch.frame.x = cross_pos;
        }
        cursor += m + gap;
    }
}

}