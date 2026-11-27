#include <algorithm>

#include "context.hpp"

using namespace std;

namespace Object {

GrowParams Context::grow_params(uint16_t id, Axis axis) const {
	if(grow_.empty())
		return {};
	const auto it{grow_.find(id)};
	if(it == grow_.end())
		return {};
	return axis == Axis::Horizontal ? it->second.h_ : it->second.v_;
}

bool Context::layout() {
	bool has_work{false};
	for(const Handle root : roots_) {
		if(!(nodes_[root.id_].flags_ & Flag::k_dirty))
			continue;
		measure(root.id_);
		arrange(root.id_);
		has_work = true;
	}
	return has_work;
}

void Context::measure(uint16_t i) {
	Node& n{nodes_[i]};
	if(!(n.flags_ & Flag::k_dirty))
		return;

	for(uint16_t c = n.first_; c != k_none; c = nodes_[c].next_)
		measure(c);

	const SizeMode wm{size_mode(n.flags_, Axis::Horizontal)};
	const SizeMode hm{size_mode(n.flags_, Axis::Vertical)};

	if(n.kind_ == Kind::Image || n.kind_ == Kind::Text) {
		auto [w, h] = n.kind_ == Kind::Image ? texture_extent(images_[n.data_].texture_)
											 : pair{texts_[n.data_].natural_w_, texts_[n.data_].natural_h_};

		if(n.kind_ == Kind::Image) {
			const UvRect& uv{images_[n.data_].uv_};
			w *= uv.u1_ - uv.u0_;
			h *= uv.v1_ - uv.v0_;
		}
		if(wm == SizeMode::Fit)
			n.frame_.width_ = w;
		if(hm == SizeMode::Fit)
			n.frame_.height_ = h;
	}

	if(n.layout_ == k_none || (wm != SizeMode::Fit && hm != SizeMode::Fit))
		return;

	const LayoutData& l{layouts_[n.layout_]};
	const bool        is_row{l.dir_ == Direction::Row};
	const Axis        main_axis{is_row ? Axis::Horizontal : Axis::Vertical};
	const Axis        cross_axis{is_row ? Axis::Vertical : Axis::Horizontal};

	const auto contribution = [this](uint16_t c, Axis a) {
		const Node& ch{nodes_[c]};
		return size_mode(ch.flags_, a) == SizeMode::Grow ? grow_params(c, a).min_ : extent(ch.frame_, a);
	};

	float    main{0.f};
	float    cross{0.f};
	unsigned count = 0;

	for(uint16_t c = n.first_; c != k_none; c = nodes_[c].next_) {
		if(!(nodes_[c].flags_ & Flag::k_visible))
			continue;
		++count;
		main += contribution(c, main_axis);
		cross = max(cross, contribution(c, cross_axis));
	}
	if(count > 1)
		main += l.spacing_ * static_cast<float>(count - 1);

	const float w{(is_row ? main : cross) + l.padding_.left_ + l.padding_.right_};
	const float h{(is_row ? cross : main) + l.padding_.top_ + l.padding_.bottom_};
	if(wm == SizeMode::Fit)
		n.frame_.width_ = w;
	if(hm == SizeMode::Fit)
		n.frame_.height_ = h;
}

void Context::arrange(uint16_t i) {
	Node& n{nodes_[i]};

	if(n.layout_ != k_none)
		arrange_children(n);

	for(uint16_t c = n.first_; c != k_none; c = nodes_[c].next_)
		if(nodes_[c].flags_ & Flag::k_dirty)
			arrange(c);

	n.flags_ &= static_cast<uint16_t>(~Flag::k_dirty);
}

float Context::resolve_grow(Node& n, Axis axis, float space) {
	const auto is_grow = [&](const Node& ch) {
		return (ch.flags_ & Flag::k_visible) && size_mode(ch.flags_, axis) == SizeMode::Grow;
	};

	unsigned remaining = 0;
	for(uint16_t c = n.first_; c != k_none; c = nodes_[c].next_)
		if(is_grow(nodes_[c]))
			++remaining;

	float frozen_sum{0.f};
	while(remaining > 0) {
		float total_weight{0.f};
		for(uint16_t c = n.first_; c != k_none; c = nodes_[c].next_) {
			const Node& ch{nodes_[c]};
			if(is_grow(ch) && !(ch.flags_ & Flag::k_frozen))
				total_weight += grow_params(c, axis).weight_;
		}

		const float avail{max(space - frozen_sum, 0.f)};
		const auto  wanted = [&](const GrowParams& p) {
			return total_weight > 0.f ? avail * p.weight_ / total_weight : 0.f;
		};

		float violation{0.f};
		for(uint16_t c = n.first_; c != k_none; c = nodes_[c].next_) {
			const Node& ch{nodes_[c]};
			if(!is_grow(ch) || (ch.flags_ & Flag::k_frozen))
				continue;
			const GrowParams p{grow_params(c, axis)};
			const float      w{wanted(p)};
			violation += clamp(w, p.min_, p.max_) - w;
		}

		for(uint16_t c = n.first_; c != k_none; c = nodes_[c].next_) {
			Node& ch{nodes_[c]};
			if(!is_grow(ch) || (ch.flags_ & Flag::k_frozen))
				continue;

			const GrowParams p{grow_params(c, axis)};
			const float      w{wanted(p)};
			const float      clamped{clamp(w, p.min_, p.max_)};

			const bool should_freeze{violation == 0.f || (violation > 0.f ? clamped > w : clamped < w)};
			if(!should_freeze)
				continue;

			float& e{extent(ch.frame_, axis)};
			if(e != clamped) {
				e = clamped;
				ch.flags_ |= Flag::k_dirty;
			}
			ch.flags_ |= Flag::k_frozen;
			frozen_sum += clamped;
			--remaining;
		}
	}

	for(uint16_t c = n.first_; c != k_none; c = nodes_[c].next_)
		nodes_[c].flags_ &= static_cast<uint16_t>(~Flag::k_frozen);

	return frozen_sum;
}

void Context::arrange_children(Node& n) {
	const LayoutData& l{layouts_[n.layout_]};
	const bool        is_row{l.dir_ == Direction::Row};
	const Axis        main_axis{is_row ? Axis::Horizontal : Axis::Vertical};
	const Axis        cross_axis{is_row ? Axis::Vertical : Axis::Horizontal};

	const float pad_main_start{is_row ? l.padding_.left_ : l.padding_.top_};
	const float pad_cross_start{is_row ? l.padding_.top_ : l.padding_.left_};
	const float pad_main_total{is_row ? l.padding_.left_ + l.padding_.right_ : l.padding_.top_ + l.padding_.bottom_};
	const float pad_cross_total{is_row ? l.padding_.top_ + l.padding_.bottom_ : l.padding_.left_ + l.padding_.right_};

	const float avail_main{max(0.f, extent(n.frame_, main_axis) - pad_main_total)};
	const float avail_cross{max(0.f, extent(n.frame_, cross_axis) - pad_cross_total)};

	float    used{0.f};
	unsigned count = 0;
	unsigned grow  = 0;
	for(uint16_t c = n.first_; c != k_none; c = nodes_[c].next_) {
		const Node& ch{nodes_[c]};
		if(!(ch.flags_ & Flag::k_visible))
			continue;
		++count;
		if(size_mode(ch.flags_, main_axis) == SizeMode::Grow)
			++grow;
		else
			used += extent(ch.frame_, main_axis);
	}
	if(count == 0)
		return;

	const float gaps{l.spacing_ * static_cast<float>(count - 1)};

	const float grown{grow > 0 ? resolve_grow(n, main_axis, avail_main - used - gaps) : 0.f};

	const float leftover{avail_main - used - gaps - grown};
	float       lead{0.f};
	float       gap{l.spacing_};
	// Negative leftover too: an overshooting spring must stay centered.
	switch(l.justify_) {
		case Justify::Start:
			break;
		case Justify::Center:
			lead = leftover * .5f;
			break;
		case Justify::End:
			lead = leftover;
			break;
		case Justify::SpaceBetween:
			if(count > 1 && leftover > 0.f)
				gap += leftover / static_cast<float>(count - 1);
			break;
	}

	float cursor{pad_main_start + lead};
	for(uint16_t c = n.first_; c != k_none; c = nodes_[c].next_) {
		Node& ch{nodes_[c]};
		if(!(ch.flags_ & Flag::k_visible))
			continue;

		const float m{extent(ch.frame_, main_axis)};
		float&      x{extent(ch.frame_, cross_axis)};

		if(size_mode(ch.flags_, cross_axis) == SizeMode::Grow) {
			const GrowParams p{grow_params(c, cross_axis)};
			const float      s{clamp(avail_cross, p.min_, p.max_)};
			if(x != s) {
				x = s;
				ch.flags_ |= Flag::k_dirty;
			}
		}

		float cross_pos{pad_cross_start};
		switch(l.align_) {
			case Alignment::Start:
				break;
			case Alignment::Center:
				cross_pos += (avail_cross - x) * .5f;
				break;
			case Alignment::End:
				cross_pos += avail_cross - x;
				break;
		}

		if(is_row) {
			ch.frame_.x_ = cursor;
			ch.frame_.y_ = cross_pos;
		} else {
			ch.frame_.y_ = cursor;
			ch.frame_.x_ = cross_pos;
		}
		cursor += m + gap;
	}
}

}
