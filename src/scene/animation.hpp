#pragma once

#include "struct.hpp"

#include <cstdint>
#include <vector>

namespace Object {
class Context;
}

enum class Prop : uint8_t {
    X, Y, W, H,
    Radius,
    Opacity,
    ColorR, ColorG, ColorB, ColorA,
};

enum class Ease : uint8_t { Linear, InQuad, OutQuad, InOutQuad, OutCubic, InOutCubic };

struct Spring {
    float stiffness = 400.f;
    float damping   = 32.f;
    float mass      = 1.f;
};

struct Timing {
    float duration = 250.f; // milliseconds
    Ease  ease     = Ease::OutCubic;
};


class Animator {
public:
    bool start(Object::Context& ctx, Object::Handle h, Prop p, float to, const Spring& s, float delay);
    bool start(Object::Context& ctx, Object::Handle h, Prop p, float to, const Timing& t, float delay);

    void cancel(Object::Handle h, Prop p); // stops where it is
    void cancel_all(Object::Handle h);

    [[nodiscard]] bool active(Object::Handle h, Prop p) const;

    [[nodiscard]] bool empty() const { return entries_.empty(); }

    bool update(Object::Context& ctx, float dt_ms);

private:
    enum class Mode : uint8_t { Spring, Timed };

    struct SpringParams {
        float k, c, m;
    };

    struct TimedParams {
        float from, duration, elapsed;
    };

    struct Entry {
        Object::Handle h{};
        Prop           prop{};
        Mode           mode{};
        Ease           ease{};
        float          value{};
        float          velocity{};
        float          target{};
        float          delay{}; // ms
        union {
            SpringParams spring;
            TimedParams  timed;
        };
    };

    [[nodiscard]] Entry* acquire(Object::Context& ctx, Object::Handle h, Prop p);

    static bool step(Entry& e, float dt); // true when finished

    std::vector<Entry> entries_;
};