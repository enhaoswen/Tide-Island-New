#include "animation.hpp"
#include "context.hpp"

#include <algorithm>
#include <cmath>

using namespace std;

namespace {

constexpr float rest_pos(Prop p) {
    return p >= Prop::Opacity ? 0.002f : 0.05f;
}

constexpr float rest_vel(Prop p) { return rest_pos(p) * 10.f; }

float ease(Ease e, float t) {
    switch (e) {
    case Ease::Linear:
        return t;
    case Ease::InQuad:
        return t * t;
    case Ease::OutQuad:
        return t * (2.f - t);
    case Ease::InOutQuad:
        return t < .5f ? 2.f * t * t : -1.f + (4.f - 2.f * t) * t;
    case Ease::OutCubic: {
        const float u = 1.f - t;
        return 1.f - u * u * u;
    }
    case Ease::InOutCubic: {
        if (t < .5f)
            return 4.f * t * t * t;
        const float u = -2.f * t + 2.f;
        return 1.f - u * u * u * .5f;
    }
    }
    return t;
}

void spring_step(float& value, float&    velocity, float target,
    float               stiffness, float damping, float  mass, float dt) noexcept {
    const double k = max(stiffness, 1e-3f);
    const double m = max(mass, 1e-4f);
    const double c = max(damping, 0.f);

    const double w0   = sqrt(k / m);
    const double zeta = c / (2.0 * sqrt(k * m));
    const double x    = static_cast<double>(value) - target;
    const double v    = velocity;
    const double t    = dt;

    double nx, nv;

    if (zeta < 1.0 - 1e-4) {
        const double wd = w0 * sqrt(1.0 - zeta * zeta);
        const double a  = zeta * w0;
        const double e  = exp(-a * t);
        const double cs = cos(wd * t);
        const double sn = sin(wd * t);
        const double B  = (v + a * x) / wd;
        nx              = e * (x * cs + B * sn);
        nv              = e * ((B * wd - a * x) * cs - (x * wd + a * B) * sn);
    }
    else if (zeta > 1.0 + 1e-4) {
        const double s  = sqrt(zeta * zeta - 1.0);
        const double r1 = -w0 * (zeta - s);
        const double r2 = -w0 * (zeta + s);
        const double c2 = (v - r1 * x) / (r2 - r1);
        const double c1 = x - c2;
        const double e1 = exp(r1 * t);
        const double e2 = exp(r2 * t);
        nx              = c1 * e1 + c2 * e2;
        nv              = c1 * r1 * e1 + c2 * r2 * e2;
    }
    else {
        const double e = exp(-w0 * t);
        const double B = v + w0 * x;
        nx             = e * (x + B * t);
        nv             = e * (v - w0 * B * t);
    }

    value    = static_cast<float>(target + nx);
    velocity = static_cast<float>(nv);
}

}

Animator::Entry* Animator::acquire(Object::Context& ctx, Object::Handle h, Prop p) {
    for (Entry& e : entries_)
        if (e.h == h && e.prop == p)
            return &e;

    const auto current = ctx.get_prop(h, p); // also rejects dead handles / unsupported props
    if (!current)
        return nullptr;

    Entry e{};
    e.h     = h;
    e.prop  = p;
    e.value = *current;
    entries_.push_back(e);
    return &entries_.back();
}

bool Animator::start(SAnimDesc& desc) {
    Entry* e = acquire(desc.ctx, desc.h, desc.prop);
    if (!e)
        return false;

    if (e->mode == Mode::Timed)
        e->velocity = 0.f;
    e->mode   = Mode::Spring;
    e->target = desc.to;
    e->delay  = max(desc.delay, 0.f);
    e->spring = {desc.spring->stiffness, desc.spring->damping, desc.spring->mass};
    return true;
}

bool Animator::start(TAnimDesc& desc) {
    Entry* e = acquire(desc.ctx, desc.h, desc.prop);
    if (!e)
        return false;

    e->mode     = Mode::Timed;
    e->ease     = desc.timing.ease;
    e->velocity = 0.f;
    e->target   = desc.to;
    e->delay    = max(desc.delay, 0.f);
    e->timed    = {e->value, desc.timing.duration, 0.f};
    return true;
}

void Animator::cancel(Object::Handle h, Prop p) {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].h == h && entries_[i].prop == p) {
            entries_[i] = entries_.back();
            entries_.pop_back();
            return;
        }
    }
}

void Animator::cancel_all(Object::Handle h) {
    for (size_t i = 0; i < entries_.size();) {
        if (entries_[i].h == h) {
            entries_[i] = entries_.back();
            entries_.pop_back();
        }
        else {
            ++i;
        }
    }
}

bool Animator::active(Object::Handle h, Prop p) const {
    for (const Entry& e : entries_)
        if (e.h == h && e.prop == p)
            return true;
    return false;
}

// dt_ms in milliseconds. Springs are solved in seconds internally.
bool Animator::step(Entry& e, float dt_ms) {
    if (e.mode == Mode::Spring) {
        spring_step(e.value, e.velocity, e.target, e.spring.k, e.spring.c, e.spring.m,
            dt_ms * 0.001f);

        if (fabs(e.value - e.target) < rest_pos(e.prop) &&
            fabs(e.velocity) < rest_vel(e.prop)) {
            e.value    = e.target;
            e.velocity = 0.f;
            return true;
        }
        return false;
    }

    TimedParams& t = e.timed;
    t.elapsed      += dt_ms;
    if (t.duration <= 0.f || t.elapsed >= t.duration) {
        e.value = e.target;
        return true;
    }
    e.value = t.from + (e.target - t.from) * ease(e.ease, t.elapsed / t.duration);
    return false;
}

bool Animator::update(Object::Context& ctx, float dt_ms) {
    dt_ms        = max(dt_ms, 0.f);
    bool stepped = false;

    for (size_t i = 0; i < entries_.size();) {
        Entry& e        = entries_[i];
        bool   finished = !ctx.alive(e.h);

        if (!finished) {
            float step_dt = dt_ms;
            if (e.delay > 0.f) {
                if (e.delay > dt_ms) {
                    e.delay -= dt_ms;
                    ++i;
                    continue;
                }
                step_dt = dt_ms - e.delay;
                e.delay = 0.f;
            }

            finished = step(e, step_dt);
            stepped  = true;
            if (!ctx.set_prop(e.h, e.prop, e.value))
                finished = true;
        }

        if (finished) {
            e = entries_.back();
            entries_.pop_back();
        }
        else {
            ++i;
        }
    }
    return stepped;
}