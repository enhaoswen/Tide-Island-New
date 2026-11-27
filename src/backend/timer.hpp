#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <source_location>

namespace Timer {

/// @brief Opaque handle to a scheduled event; validity is checked against the event queue.
struct EventHandle {
	uint32_t index_{std::numeric_limits<uint32_t>::max()};
	uint32_t generation_{};
	/// @brief Compares index and generation.
	bool operator==(const EventHandle&) const = default;
};

static_assert(sizeof(EventHandle) == 8);

/// @brief Initializes the timerfd; no-op if already initialized.
void init();

// Priority orders simultaneous deadlines; it never suppresses an event.
/// @brief Schedules callback after duration; returns a handle for cancellation.
EventHandle push(std::chrono::microseconds duration,
	void (*callback)(),
	float                priority,
	std::source_location location = std::source_location::current());

/// @brief Cancels a scheduled event; returns true if it was still pending.
bool cancel(EventHandle event);

/// @brief Returns true if the event is still scheduled.
bool is_pending(EventHandle event);

/// @brief Cancels every scheduled event.
void clear();

/// @brief Cancels every event and closes the timerfd.
void shutdown();

/// @brief Drains the timerfd and invokes due callbacks.
void handle_events(std::source_location location = std::source_location::current());

/// @brief Returns the timerfd to poll for readiness.
int get_timer_fd();

}
