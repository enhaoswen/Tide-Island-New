#include "context.hpp"

#include <algorithm>

// Two-pass stack layout, restricted to dirty subtrees.
//
//   measure()  bottom-up : resolves Fit sizes (texture size / sum of children)
//   arrange()  top-down  : distributes space, writes child positions and the
//                          sizes of Grow axes
//
// Rules that keep it cheap and predictable:
//  * Fixed axes are never written by layout.
//  * Positions are local to the parent, so a child whose position changed
//    never needs its own subtree revisited - only a changed SIZE does, and
//    arrange() flags such children dirty on the spot.
//  * Hidden children take no space but are still traversed so their dirty
//    flags get cleared (otherwise the "dirty => ancestors dirty" invariant
//    would break).
//  * A node without a layout does not position its children; Fit on such a
//    container is treated as Fixed.

using namespace std;

namespace Object {

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
    Node& node = nodes_[i]; // storage is not resized during layout
    if (!(node.flags & flag::dirty))
        return;

    for (uint16_t c = node.first; c != kNone; c = nodes_[c].next)
        measure(c);

    const SizeMode wm = size_mode(node.flags, Axis::Horizontal);
    const SizeMode hm = size_mode(node.flags, Axis::Vertical);

    if ((node.kind == Kind::Image || node.kind == Kind::Text) &&
        (wm == SizeMode::Fit || hm == SizeMode::Fit)) {

        const Texture& tex = node.kind == Kind::Image
            ? images_[node.data].texture
            : texts_[node.data].texture;

        const auto [w, h] = texture_extent(tex);

        if (wm == SizeMode::Fit)
            node.frame.width = w;
        if (hm == SizeMode::Fit)
            node.frame.height = h;
    }

    if (node.layout == kNone || (wm != SizeMode::Fit && hm != SizeMode::Fit))
        return;

    const LayoutData& l          = layouts_[node.layout];
    const bool        row        = l.dir == Direction::Row;
    const Axis        main_axis  = row ? Axis::Horizontal : Axis::Vertical;
    const Axis        cross_axis = row ? Axis::Vertical : Axis::Horizontal;

    float    main  = 0.f;
    float    cross = 0.f;
    unsigned count = 0;

    for (uint16_t c = node.first; c != kNone; c = nodes_[c].next) {
        const Node& ch = nodes_[c];
        if (!(ch.flags & flag::visible))
            continue;
        ++count;
        // Grow children have no intrinsic size: they contribute nothing.
        if (size_mode(ch.flags, main_axis) != SizeMode::Grow)
            main += extent(ch.frame, main_axis);
        if (size_mode(ch.flags, cross_axis) != SizeMode::Grow)
            cross = max(cross, extent(ch.frame, cross_axis));
    }
    if (count > 1)
        main += l.spacing * static_cast<float>(count - 1);

    const float w = (row ? main : cross) + l.padding.left + l.padding.right;
    const float h = (row ? cross : main) + l.padding.top + l.padding.bottom;
    if (wm == SizeMode::Fit)
        node.frame.width = w;
    if (hm == SizeMode::Fit)
        node.frame.height = h;
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

void Context::arrange_children(Node& n) {
    const LayoutData& L          = layouts_[n.layout];
    const bool        row        = L.dir == Direction::Row;
    const Axis        main_axis  = row ? Axis::Horizontal : Axis::Vertical;
    const Axis        cross_axis = row ? Axis::Vertical : Axis::Horizontal;

    const float pad_main_start  = row ? L.padding.left : L.padding.top;
    const float pad_cross_start = row ? L.padding.top : L.padding.left;
    const float pad_main_total  = row ? L.padding.left + L.padding.right : L.padding.top + L.padding.bottom;
    const float pad_cross_total = row ? L.padding.top + L.padding.bottom : L.padding.left + L.padding.right;

    const float avail_main  = max(0.f, extent(n.frame, main_axis) - pad_main_total);
    const float avail_cross = max(0.f, extent(n.frame, cross_axis) - pad_cross_total);

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

    const float gaps       = L.spacing * static_cast<float>(count - 1);
    const float free_space = avail_main - used - gaps; // may be negative (overflow)

    float grow_size = 0.f;
    float lead      = 0.f;
    float gap       = L.spacing;
    if (grow > 0) {
        grow_size = max(free_space, 0.f) / static_cast<float>(grow);
    }
    else if (free_space > 0.f) {
        switch (L.justify) {
        case Justify::Start:
            break;
        case Justify::Center:
            lead = free_space * .5f;
            break;
        case Justify::End:
            lead = free_space;
            break;
        case Justify::SpaceBetween:
            if (count > 1)
                gap += free_space / static_cast<float>(count - 1);
            break;
        }
    }

    float cursor = pad_main_start + lead;
    for (uint16_t c = n.first; c != kNone; c = nodes_[c].next) {
        Node& ch = nodes_[c];
        if (!(ch.flags & flag::visible))
            continue;

        float& m = extent(ch.frame, main_axis);
        float& x = extent(ch.frame, cross_axis);

        if (size_mode(ch.flags, main_axis) == SizeMode::Grow && m != grow_size) {
            m        = grow_size;
            ch.flags |= flag::dirty;
        }
        if (size_mode(ch.flags, cross_axis) == SizeMode::Grow && x != avail_cross) {
            x        = avail_cross;
            ch.flags |= flag::dirty;
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