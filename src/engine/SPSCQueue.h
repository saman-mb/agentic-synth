#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <optional>
#include <type_traits>

#include "PatchStruct.h"

namespace agentic_synth {

// ---------------------------------------------------------------------------
// SPSCQueue — single-producer / single-consumer wait-free ring buffer.
//
// Guarantees:
//   - No dynamic allocation after construction.
//   - No locks, no mutexes, no syscalls.
//   - Exactly one thread may call push(); exactly one may call pop().
//   - Capacity must be a power of two; enforced by static_assert.
//
// Usage:
//   SPSCQueue<PatchStruct, 256> queue;      // producer (UI / agent thread)
//   queue.push(patch);
//
//   if (auto p = queue.pop()) { ... }       // consumer (audio thread)
// ---------------------------------------------------------------------------

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert(std::is_trivially_copyable_v<T>,
                  "SPSCQueue requires a trivially copyable element type");
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "SPSCQueue Capacity must be a power of two >= 2");

public:
    SPSCQueue() noexcept = default;

    // Not copyable or movable — contains atomic members.
    SPSCQueue(const SPSCQueue&)            = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&)                 = delete;
    SPSCQueue& operator=(SPSCQueue&&)      = delete;

    // -----------------------------------------------------------------------
    // push — called ONLY by the producer thread.
    // Returns true on success, false when the queue is full (item dropped).
    // -----------------------------------------------------------------------
    [[nodiscard]] bool push(const T& item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & kMask;

        if (next == tail_.load(std::memory_order_acquire))
            return false; // full

        std::memcpy(&storage_[head], &item, sizeof(T));
        head_.store(next, std::memory_order_release);
        return true;
    }

    // -----------------------------------------------------------------------
    // pop — called ONLY by the consumer thread (audio thread).
    // Returns the oldest item, or std::nullopt if the queue is empty.
    // -----------------------------------------------------------------------
    [[nodiscard]] std::optional<T> pop() noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire))
            return std::nullopt; // empty

        T item;
        std::memcpy(&item, &storage_[tail], sizeof(T));
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return item;
    }

    // -----------------------------------------------------------------------
    // drain_latest — discard all but the newest pending item.
    // Audio thread uses this to skip stale patches when multiple piled up.
    // -----------------------------------------------------------------------
    [[nodiscard]] std::optional<T> drain_latest() noexcept {
        std::optional<T> latest;
        while (auto item = pop())
            latest = std::move(item);
        return latest;
    }

    // -----------------------------------------------------------------------
    // Diagnostic observers — do NOT use for real-time flow control.
    // -----------------------------------------------------------------------
    [[nodiscard]] bool empty() const noexcept {
        return tail_.load(std::memory_order_acquire) ==
               head_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        const std::size_t h = head_.load(std::memory_order_acquire);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & kMask;
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    // Separate cache lines for head (producer) and tail (consumer) to prevent
    // false sharing, which would introduce cache-coherence stalls on the
    // audio thread.
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    alignas(64) T storage_[Capacity];
};

// ---------------------------------------------------------------------------
// Convenience alias for the canonical patch delivery path.
// 256 slots: at ~1 patch/block, this is >5 s of headroom at 44100 Hz /
// 512 frames before the producer starts dropping patches.
// ---------------------------------------------------------------------------
using PatchQueue = SPSCQueue<PatchStruct, 256>;

} // namespace agentic_synth
