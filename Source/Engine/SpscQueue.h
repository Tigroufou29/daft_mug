#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <utility>

namespace dm
{
// Wait-free single-producer / single-consumer ring buffer.
// Used to move commands message thread -> audio thread and retired samples audio thread -> message thread.
// No allocation, no lock: safe to use from the audio callback.
template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    bool push (T&& value) noexcept
    {
        const auto h = head.load (std::memory_order_relaxed);
        const auto next = (h + 1) & mask;
        if (next == tail.load (std::memory_order_acquire))
            return false;   // full: `value` is left untouched
        buffer[h] = std::move (value);
        head.store (next, std::memory_order_release);
        return true;
    }

    bool pop (T& out) noexcept
    {
        const auto t = tail.load (std::memory_order_relaxed);
        if (t == head.load (std::memory_order_acquire))
            return false;
        out = std::move (buffer[t]);
        buffer[t] = T{};
        tail.store ((t + 1) & mask, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t mask = Capacity - 1;
    std::array<T, Capacity> buffer {};
    std::atomic<std::size_t> head { 0 };
    std::atomic<std::size_t> tail { 0 };
};
} // namespace dm
