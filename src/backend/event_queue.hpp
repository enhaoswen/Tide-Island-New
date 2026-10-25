#pragma once

#include "backend/event_handle.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Timer {

namespace Detail {

// Indexed min-heap: O(log n) push/cancel/pop, O(1) validation/top.
// Storage is reused; cancellation removes an entry immediately, without tombstones.
class EventQueue {
public:
    using Clock = std::chrono::steady_clock;
    using Callback = void (*)();
private:
    static constexpr auto none = std::numeric_limits<std::uint32_t>::max();
    struct Slot {
        Clock::time_point deadline{};
        Callback callback{};
        float priority{};
        std::uint32_t position{none}; // Heap position when live; next free slot otherwise.
        std::uint32_t generation{1};
    };
    std::vector<Slot> slots{};
    std::vector<std::uint32_t> heap{};
    std::uint32_t free_slot{none};

    bool earlier(std::uint32_t a, std::uint32_t b) const {
        if (slots[a].deadline != slots[b].deadline) return slots[a].deadline < slots[b].deadline;
        if (slots[a].priority != slots[b].priority) return slots[a].priority > slots[b].priority;
        return a < b;
    }
    void exchange(std::uint32_t a, std::uint32_t b) {
        std::swap(heap[a], heap[b]);
        slots[heap[a]].position = a;
        slots[heap[b]].position = b;
    }
    void up(std::uint32_t position) {
        while (position && earlier(heap[position], heap[(position - 1) / 2])) {
            const auto parent = (position - 1) / 2;
            exchange(position, parent);
            position = parent;
        }
    }
    void down(std::uint32_t position) {
        for (;;) {
            const auto left = static_cast<std::size_t>(position) * 2 + 1;
            if (left >= heap.size()) return;
            auto child = static_cast<std::uint32_t>(left);
            if (left + 1 < heap.size() && earlier(heap[left + 1], heap[left])) ++child;
            if (!earlier(heap[child], heap[position])) return;
            exchange(position, child);
            position = child;
        }
    }
    void erase(std::uint32_t index) {
        const auto position = slots[index].position;
        const auto moved = heap.back();
        heap.pop_back();
        if (position < heap.size()) {
            heap[position] = moved;
            slots[moved].position = position;
            if (position && earlier(moved, heap[(position - 1) / 2])) up(position);
            else down(position);
        }
        auto& slot = slots[index];
        slot.callback = nullptr;
        if (slot.generation != none) ++slot.generation;
        if (slot.generation != none) {
            slot.position = free_slot;
            free_slot = index;
        }
    }
public:
    [[nodiscard]] bool contains(EventHandle handle) const {
        return handle.index < slots.size() && slots[handle.index].callback &&
            slots[handle.index].generation == handle.generation;
    }
    [[nodiscard]] bool empty() const { return heap.empty(); }
    [[nodiscard]] std::size_t size() const { return heap.size(); }
    [[nodiscard]] Clock::time_point next_deadline() const {
        return heap.empty() ? Clock::time_point::max() : slots[heap.front()].deadline;
    }
    EventHandle push(Clock::time_point deadline, Callback callback, float priority = 0) {
        if (!callback) return {};
        std::uint32_t index{};
        if (free_slot != none) {
            index = free_slot;
            free_slot = slots[index].position;
        }
        else {
            if (slots.size() >= none) throw std::length_error("Timer event limit reached");
            index = static_cast<std::uint32_t>(slots.size());
            slots.emplace_back();
        }
        auto& slot = slots[index];
        slot.deadline = deadline;
        slot.priority = priority;
        slot.callback = callback;
        slot.position = static_cast<std::uint32_t>(heap.size());
        heap.push_back(index);
        up(slot.position);
        return {index, slot.generation};
    }
    bool cancel(EventHandle handle) {
        if (!contains(handle)) return false;
        erase(handle.index);
        return true;
    }
    Callback pop_ready(Clock::time_point now) {
        if (heap.empty() || slots[heap.front()].deadline > now) return nullptr;
        const auto index = heap.front();
        const auto callback = slots[index].callback;
        erase(index); // Invalidate before invoking, allowing safe cancellation/rescheduling.
        return callback;
    }
    void clear() {
        // Linear rather than repeatedly repairing the heap.
        for (const auto index : heap) {
            auto& slot = slots[index];
            slot.callback = nullptr;
            if (slot.generation != none) ++slot.generation;
            if (slot.generation != none) {
                slot.position = free_slot;
                free_slot = index;
            }
        }
        heap.clear();
    }
};

} // namespace Detail
} // namespace Timer
