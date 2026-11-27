#include "animation.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "context.hpp"

using namespace std;

namespace {

constexpr float rest_pos(Prop p) {
	return p >= Prop::Opacity && p != Prop::Thickness ? 0.002f : 0.05f;
}

constexpr float rest_vel(Prop p) {
	return rest_pos(p) * 10.f;
}

float ease(Ease e, float t) {
	switch(e) {
		case Ease::Linear:
			return t;
		case Ease::InQuad:
			return t * t;
		case Ease::OutQuad:
			return t * (2.f - t);
		case Ease::InOutQuad:
			return t < .5f ? 2.f * t * t : -1.f + (4.f - 2.f * t) * t;
		case Ease::OutCubic: {
			const float u{1.f - t};
			return 1.f - u * u * u;
		}
		case Ease::InOutCubic: {
			if(t < .5f)
				return 4.f * t * t * t;
			const float u{-2.f * t + 2.f};
			return 1.f - u * u * u * .5f;
		}
	}
	return t;
}

void spring_step(float& value, float& velocity, float target, float stiffness, float damping, float mass, float dt) {
	const double k{max(stiffness, 1e-3f)};
	const double m{max(mass, 1e-4f)};
	const double c{max(damping, 0.f)};

	const double w0{sqrt(k / m)};
	const double zeta{c / (2.0 * sqrt(k * m))};
	const double x{static_cast<double>(value) - target};
	const double v{velocity};
	const double t{dt};

	double nx, nv;

	if(zeta < 1.0 - 1e-4) {
		const double wd{w0 * sqrt(1.0 - zeta * zeta)};
		const double a{zeta * w0};
		const double e{exp(-a * t)};
		const double cs{cos(wd * t)};
		const double sn{sin(wd * t)};
		const double b{(v + a * x) / wd};
		nx = e * (x * cs + b * sn);
		nv = e * ((b * wd - a * x) * cs - (x * wd + a * b) * sn);
	} else if(zeta > 1.0 + 1e-4) {
		const double s{sqrt(zeta * zeta - 1.0)};
		const double r1{-w0 * (zeta - s)};
		const double r2{-w0 * (zeta + s)};
		const double c2{(v - r1 * x) / (r2 - r1)};
		const double c1{x - c2};
		const double e1{exp(r1 * t)};
		const double e2{exp(r2 * t)};
		nx = c1 * e1 + c2 * e2;
		nv = c1 * r1 * e1 + c2 * r2 * e2;
	} else {
		const double e{exp(-w0 * t)};
		const double b{v + w0 * x};
		nx = e * (x + b * t);
		nv = e * (v - w0 * b * t);
	}

	value    = static_cast<float>(target + nx);
	velocity = static_cast<float>(nv);
}

}

Animator::Entry* Animator::acquire(Object::Context& ctx, Object::Handle h, Prop p) {
	for(Entry& e : entries_)
		if(e.h_ == h && e.prop_ == p)
			return &e;

	const auto current{ctx.get_prop(h, p)}; // also rejects dead handles / unsupported props
	if(!current)
		return nullptr;

	Entry e{};
	e.h_     = h;
	e.prop_  = p;
	e.value_ = *current;
	entries_.push_back(e);
	return &entries_.back();
}

bool Animator::start(Object::Context& ctx,
	Object::Handle                    h,
	Prop                              p,
	float                             to,
	const Spring&                     s,
	float                             delay,
	AnimCallback                      on_done) {
	Entry* e{acquire(ctx, h, p)};
	if(!e)
		return false;

	if(e->mode_ == Mode::Timed)
		e->velocity_ = 0.f;
	e->mode_    = Mode::Spring;
	e->target_  = to;
	e->delay_   = std::max(delay, 0.f);
	e->on_done_ = std::move(on_done);
	e->spring_  = {s.stiffness_, s.damping_, s.mass_};
	return true;
}

bool Animator::start(Object::Context& ctx,
	Object::Handle                    h,
	Prop                              p,
	float                             to,
	const Timing&                     t,
	float                             delay,
	AnimCallback                      on_done) {
	Entry* e{acquire(ctx, h, p)};
	if(!e)
		return false;

	e->mode_     = Mode::Timed;
	e->ease_     = t.ease_;
	e->velocity_ = 0.f;
	e->target_   = to;
	e->delay_    = std::max(delay, 0.f);
	e->on_done_  = std::move(on_done);
	e->timed_    = {e->value_, t.duration_, 0.f};
	return true;
}

void Animator::cancel(Object::Handle h, Prop p) {
	for(size_t i = 0; i < entries_.size(); ++i) {
		if(entries_[i].h_ == h && entries_[i].prop_ == p) {
			entries_[i] = entries_.back();
			entries_.pop_back();
			return;
		}
	}
}

void Animator::cancel_all(Object::Handle h) {
	for(size_t i = 0; i < entries_.size();) {
		if(entries_[i].h_ == h) {
			entries_[i] = entries_.back();
			entries_.pop_back();
		} else {
			++i;
		}
	}
}

bool Animator::is_active(Object::Handle h, Prop p) const {
	return ranges::any_of(entries_, [h, p](const Entry& e) { return e.h_ == h && e.prop_ == p; });
}

// dt_ms in milliseconds. Springs are solved in seconds internally.
bool Animator::step(Entry& e, float dt_ms) {
	if(e.mode_ == Mode::Spring) {
		spring_step(e.value_, e.velocity_, e.target_, e.spring_.k_, e.spring_.c_, e.spring_.m_, dt_ms * 0.001f);

		if(fabs(e.value_ - e.target_) < rest_pos(e.prop_) && fabs(e.velocity_) < rest_vel(e.prop_)) {
			e.value_    = e.target_;
			e.velocity_ = 0.f;
			return true;
		}
		return false;
	}

	TimedParams& t{e.timed_};
	t.elapsed_ += dt_ms;
	if(t.duration_ <= 0.f || t.elapsed_ >= t.duration_) {
		e.value_ = e.target_;
		return true;
	}
	e.value_ = t.from_ + (e.target_ - t.from_) * ease(e.ease_, t.elapsed_ / t.duration_);
	return false;
}

bool Animator::update(Object::Context& ctx, float dt_ms) {
	dt_ms = max(dt_ms, 0.f);
	bool has_stepped{false};

	// Callbacks run after the loop: they may start/cancel animations and mutate entries_.
	vector<AnimCallback> done;

	for(size_t i = 0; i < entries_.size();) {
		Entry& e{entries_[i]};
		bool   is_finished{!ctx.is_alive(e.h_)};
		bool   has_reached{false};

		if(!is_finished) {
			float step_dt{dt_ms};
			if(e.delay_ > 0.f) {
				if(e.delay_ > dt_ms) {
					e.delay_ -= dt_ms;
					++i;
					continue;
				}
				step_dt  = dt_ms - e.delay_;
				e.delay_ = 0.f;
			}

			is_finished = step(e, step_dt);
			has_reached = is_finished;
			has_stepped = true;
			if(!ctx.set_prop(e.h_, e.prop_, e.value_)) {
				is_finished = true;
				has_reached = false;
			}
		}

		if(is_finished) {
			if(has_reached && e.on_done_)
				done.push_back(std::move(e.on_done_));
			e = entries_.back();
			entries_.pop_back();
		} else {
			++i;
		}
	}

	for(auto& cb : done)
		cb();

	return has_stepped;
}
