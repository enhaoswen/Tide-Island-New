#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "backend/timer.hpp"

namespace Timer::Detail {

/// @brief Timer scheduler: a slot-backed binary heap ordered by deadline, then priority, then slot index.
class EventQueue {
public:
	using Clock    = std::chrono::steady_clock;
	using Callback = void (*)();

	/// @brief Returns true if the handle refers to a live event.
	[[nodiscard]] bool has_event(EventHandle handle) const {
		return handle.index_ < slots_.size() && slots_[handle.index_].callback_ &&
			slots_[handle.index_].generation_ == handle.generation_;
	}

	/// @brief Returns true if no events are scheduled.
	[[nodiscard]] bool is_empty() const { return heap_.empty(); }

	/// @brief Returns the number of scheduled events.
	[[nodiscard]] size_t size() const { return heap_.size(); }

	/// @brief Returns the earliest scheduled deadline, or time_point::max() if empty.
	[[nodiscard]] Clock::time_point get_next_deadline() const {
		return heap_.empty() ? Clock::time_point::max() : slots_[heap_.front()].deadline_;
	}

	/// @brief Schedules callback at deadline with the given priority; returns a handle, or {} if callback is null.
	EventHandle push(Clock::time_point deadline, Callback callback, float priority = 0.f) {
		if(!callback)
			return {};
		uint32_t index{};
		if(free_slot_ != k_none) {
			index      = free_slot_;
			free_slot_ = slots_[index].position_;
		} else {
			if(slots_.size() >= k_none)
				throw std::length_error("Timer event limit reached");
			index = static_cast<uint32_t>(slots_.size());
			slots_.emplace_back();
		}
		auto& slot{slots_[index]};
		slot.deadline_ = deadline;
		slot.priority_ = priority;
		slot.callback_ = callback;
		slot.position_ = static_cast<uint32_t>(heap_.size());
		heap_.push_back(index);
		up(slot.position_);
		return {index, slot.generation_};
	}

	/// @brief Cancels a live event; returns true if the handle was valid.
	bool cancel(EventHandle handle) {
		if(!has_event(handle))
			return false;
		erase(handle.index_);
		return true;
	}

	/// @brief Removes and returns the callback of a ready event, or nullptr if none is ready.
	Callback pop_ready(Clock::time_point now) {
		if(heap_.empty() || slots_[heap_.front()].deadline_ > now)
			return nullptr;
		const auto index{heap_.front()};
		const auto callback{slots_[index].callback_};
		erase(index); // Invalidate before invoking, allowing safe cancellation/rescheduling.
		return callback;
	}

	/// @brief Cancels every scheduled event.
	void clear() {
		// Linear rather than repeatedly repairing the heap.
		for(const auto index : heap_) {
			auto& slot{slots_[index]};
			slot.callback_ = nullptr;
			if(slot.generation_ != k_none)
				++slot.generation_;
			if(slot.generation_ != k_none) {
				slot.position_ = free_slot_;
				free_slot_     = index;
			}
		}
		heap_.clear();
	}

private:
	static constexpr uint32_t k_none{std::numeric_limits<uint32_t>::max()};

	struct Slot {
		Clock::time_point deadline_{};
		Callback          callback_{};
		float             priority_{};
		uint32_t          position_{k_none}; // Heap position when live; next free slot otherwise.
		uint32_t          generation_{1};
	};

	std::vector<Slot>     slots_{};
	std::vector<uint32_t> heap_{};
	uint32_t              free_slot_{k_none};

	[[nodiscard]] bool is_earlier(uint32_t a, uint32_t b) const {
		if(slots_[a].deadline_ != slots_[b].deadline_)
			return slots_[a].deadline_ < slots_[b].deadline_;
		if(slots_[a].priority_ != slots_[b].priority_)
			return slots_[a].priority_ > slots_[b].priority_;
		return a < b;
	}

	void exchange(uint32_t a, uint32_t b) {
		std::swap(heap_[a], heap_[b]);
		slots_[heap_[a]].position_ = a;
		slots_[heap_[b]].position_ = b;
	}

	void up(uint32_t position) {
		while(position && is_earlier(heap_[position], heap_[(position - 1) / 2])) {
			const auto parent{(position - 1) / 2};
			exchange(position, parent);
			position = parent;
		}
	}

	void down(uint32_t position) {
		for(;;) {
			const auto left{static_cast<size_t>(position) * 2 + 1};
			if(left >= heap_.size())
				return;
			auto child{static_cast<uint32_t>(left)};
			if(left + 1 < heap_.size() && is_earlier(heap_[left + 1], heap_[left]))
				++child;
			if(!is_earlier(heap_[child], heap_[position]))
				return;
			exchange(position, child);
			position = child;
		}
	}

	void erase(uint32_t index) {
		const auto position{slots_[index].position_};
		const auto moved{heap_.back()};
		heap_.pop_back();
		if(position < heap_.size()) {
			heap_[position]         = moved;
			slots_[moved].position_ = position;
			if(position && is_earlier(moved, heap_[(position - 1) / 2]))
				up(position);
			else
				down(position);
		}
		auto& slot{slots_[index]};
		slot.callback_ = nullptr;
		if(slot.generation_ != k_none)
			++slot.generation_;
		if(slot.generation_ != k_none) {
			slot.position_ = free_slot_;
			free_slot_     = index;
		}
	}
};

}
