#pragma once
#include <atomic>
#include <cstddef>

// Lock-free single-producer / single-consumer ring buffer.
//
// Producer = control plane (main loop, e.g. MIDI parsing); consumer = audio
// callback (ISR context). This is the only sanctioned way note events cross the
// control/audio boundary — no locks, no allocation, bounded capacity.
template <typename T, size_t Capacity>
class SpscQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

  public:
    // Producer side. Returns false if full (event dropped).
    bool Push(const T& item)
    {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & kMask;
        if(next == tail_.load(std::memory_order_acquire))
            return false; // full
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false if empty.
    bool Pop(T& out)
    {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if(tail == head_.load(std::memory_order_acquire))
            return false; // empty
        out = buffer_[tail];
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

  private:
    static constexpr size_t kMask = Capacity - 1;
    T                       buffer_[Capacity];
    std::atomic<size_t>     head_{0};
    std::atomic<size_t>     tail_{0};
};
