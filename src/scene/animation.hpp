#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "struct.hpp"

namespace Object {
class Context;
}

/// @brief Property that can be animated.
enum class Prop : uint8_t {
	X,
	Y,
	W,
	H,
	Radius,
	Opacity,
	Scale,
	ColorR,
	ColorG,
	ColorB,
	ColorA,
	Progress,
	Thickness,
};

/// @brief Callback invoked when an animation finishes.
using AnimCallback = std::function<void()>;

/// @brief Easing curve used by timed animations.
enum class Ease : uint8_t {
	Linear,
	InQuad,
	OutQuad,
	InOutQuad,
	OutCubic,
	InOutCubic,
};

/// @brief Physical parameters of a spring animation.
struct Spring {
	float stiffness_{400.f};
	float damping_{32.f};
	float mass_{1.f};
};

namespace Springs {

inline constexpr Spring k_smooth{280.f, 28.f, 1.f};
inline constexpr Spring k_snappy{400.f, 32.f, 1.f};
inline constexpr Spring k_bouncy{300.f, 18.f, 1.f};
inline constexpr Spring k_gentle{200.f, 24.f, 1.f};

}

/// @brief Duration and easing of a timed animation.
struct Timing {
	float duration_{250.f}; // milliseconds
	Ease  ease_{Ease::OutCubic};
};

namespace Timings {

inline constexpr Timing k_instant{90.f, Ease::OutQuad};
inline constexpr Timing k_quick{140.f, Ease::OutCubic};
inline constexpr Timing k_smooth{250.f, Ease::OutCubic};
inline constexpr Timing k_gentle{400.f, Ease::InOutCubic};

}

/// @brief Drives spring and timed animations of node properties.
class Animator {
public:
	/// @brief Starts a spring animation; returns false for an invalid handle or property.
	bool start(Object::Context& ctx,
		Object::Handle          h,
		Prop                    p,
		float                   to,
		const Spring&           s,
		float                   delay,
		AnimCallback            on_done = {});

	/// @brief Starts a timed animation; returns false for an invalid handle or property.
	bool start(Object::Context& ctx,
		Object::Handle          h,
		Prop                    p,
		float                   to,
		const Timing&           t,
		float                   delay,
		AnimCallback            on_done = {});

	/// @brief Stops the animation of a property where it currently is.
	void cancel(Object::Handle h, Prop p);

	/// @brief Stops all animations of a node.
	void cancel_all(Object::Handle h);

	/// @brief Returns true if the property is currently animated.
	[[nodiscard]] bool is_active(Object::Handle h, Prop p) const;

	/// @brief Returns true if no animations are running.
	[[nodiscard]] bool is_empty() const { return entries_.empty(); }

	/// @brief Advances all animations by dt_ms milliseconds; returns true if any stepped.
	bool update(Object::Context& ctx, float dt_ms);

private:
	enum class Mode : uint8_t {
		Spring,
		Timed,
	};

	struct SpringParams {
		float k_, c_, m_;
	};

	struct TimedParams {
		float from_, duration_, elapsed_;
	};

	struct Entry {
		Object::Handle h_{};
		Prop           prop_{};
		Mode           mode_{};
		Ease           ease_{};
		float          value_{};
		float          velocity_{};
		float          target_{};
		float          delay_{}; // ms
		AnimCallback   on_done_{};

		union {
			SpringParams spring_;
			TimedParams  timed_;
		};
	};

	[[nodiscard]] Entry* acquire(Object::Context& ctx, Object::Handle h, Prop p);

	static bool step(Entry& e, float dt); // true when finished

	std::vector<Entry> entries_;
};
