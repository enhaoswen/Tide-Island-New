#pragma once

#include <cstdint>

#include "context.hpp"

namespace Object {

/// @brief Concept for a renderer able to draw scene primitives.
template<class R>
concept Renderer = requires(R& r,
	const Frame&               f,
	const Color&               c,
	const Texture&             t,
	const FontHandle&          font,
	float                      x,
	const UvRect&              uv) {
	r.draw_rect(f, c, x /*radius*/, x /*opacity*/);
	r.draw_image(f, t, uv, x /*radius*/, x /*opacity*/);
	r.draw_text(f, t, font, c, x /*opacity*/);
	r.draw_ring(f, c, c /*track*/, x /*progress*/, x /*thickness*/, x /*opacity*/);
	r.push_clip(f, x /*radius*/);
	r.pop_clip();
};

/// @brief Recursive scene graph drawer.
struct Drawer {
	/// @brief Draws a node and its subtree.
	template<class R>
	static void node(Context& c, R& r, uint16_t i, const Placed& parent, float alpha) {
		const Node& n{c.nodes_[i]};
		if(!(n.flags_ & Flag::k_visible))
			return;

		alpha *= static_cast<float>(n.opacity_) * (1.f / 255.f);
		if(alpha <= 0.f)
			return;

		const Placed p{c.place(n, parent)};
		const Frame& abs{p.frame_};

		float radius{0.f};
		switch(n.kind_) {
			case Kind::Group:
				break;
			case Kind::Rect: {
				const RectData& d{c.rects_[n.data_]};
				radius = d.radius_ * p.scale_;
				r.draw_rect(abs, d.color_, radius, alpha);
				break;
			}
			case Kind::Image: {
				const ImageData& d{c.images_[n.data_]};
				radius = d.radius_ * p.scale_;
				r.draw_image(abs, d.texture_, d.uv_, radius, alpha);
				break;
			}
			case Kind::Text: {
				TextData&   d{c.texts_[n.data_]};
				const Frame f{c.prepare_text_draw(d, abs)};
				r.draw_text(f, d.texture_, d.font_, d.color_, alpha);
				break;
			}
			case Kind::Ring: {
				const RingData& d{c.rings_[n.data_]};
				r.draw_ring(abs, d.color_, d.track_, d.progress_, d.thickness_ * p.scale_, alpha);
				break;
			}
		}

		const bool is_clip{(n.flags_ & Flag::k_clip) != 0};
		if(is_clip)
			r.push_clip(abs, radius);

		for(uint16_t ch = n.first_; ch != k_none; ch = c.nodes_[ch].next_)
			node(c, r, ch, p, alpha);

		if(is_clip)
			r.pop_clip();
	}

	/// @brief Draws every root node.
	template<class R>
	static void all(Context& c, R& r) {
		for(const Handle root : c.roots_)
			node(c, r, root.id_, Placed{}, 1.f);
	}
};

/// @brief Draws the whole scene graph with the given renderer.
template<Renderer R>
void draw(Context& ctx, R& renderer) {
	Drawer::all(ctx, renderer);
}

}
