#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace agentic_synth::jsi {

// Wait-free SPSC ring. Capacity is a power of two; one slot is reserved so
// full and empty are distinct (usable = Capacity - 1). No malloc after the
// owning object is constructed. src/jsi includes only this local POD ring
// plus agsynth.h — never VoiceManager / engine/SPSCQueue.h.
template <typename T, std::size_t Capacity> class SpscRing {
    static_assert(std::is_trivially_copyable_v<T>, "SpscRing requires a trivially copyable element");
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "SpscRing Capacity must be a power of two >= 2");

public:
    SpscRing() noexcept = default;
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    [[nodiscard]] bool push(const T& item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire))
            return false;
        std::memcpy(&storage_[head], &item, sizeof(T));
        head_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool pop(T& out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        std::memcpy(&out, &storage_[tail], sizeof(T));
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool drain_latest(T& out) noexcept {
        bool any = false;
        T item{};
        while (pop(item)) {
            out = item;
            any = true;
        }
        return any;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    alignas(64) T storage_[Capacity];
};

// Two-slot latest-wins mailbox. Producer always publishes (drops stale).
// Consumer copies the newest stable slot via a seqlock so a concurrent
// overwrite cannot tear the POD. Cap is 2 (power of two).
template <typename T> class LatestWins2 {
    static_assert(std::is_trivially_copyable_v<T>, "LatestWins2 requires a trivially copyable element");

public:
    LatestWins2() noexcept = default;
    LatestWins2(const LatestWins2&) = delete;
    LatestWins2& operator=(const LatestWins2&) = delete;

    void push(const T& item) noexcept {
        const uint32_t pub = published_.load(std::memory_order_relaxed) + 1u;
        Slot& s = slots_[pub & 1u];
        const uint32_t seq = s.seq.load(std::memory_order_relaxed);
        s.seq.store(seq + 1u, std::memory_order_relaxed); // odd = write in progress
        std::memcpy(&s.data, &item, sizeof(T));
        s.seq.store(seq + 2u, std::memory_order_release); // even = stable
        published_.store(pub, std::memory_order_release);
    }

    [[nodiscard]] bool drain_latest(T& out) noexcept {
        const uint32_t pub = published_.load(std::memory_order_acquire);
        if (pub == consumed_)
            return false;
        Slot& s = slots_[pub & 1u];
        for (;;) {
            const uint32_t a = s.seq.load(std::memory_order_acquire);
            if ((a & 1u) != 0u)
                continue;
            std::memcpy(&out, &s.data, sizeof(T));
            const uint32_t b = s.seq.load(std::memory_order_acquire);
            if (a == b) {
                consumed_ = pub;
                return true;
            }
        }
    }

private:
    struct Slot {
        alignas(64) std::atomic<uint32_t> seq{0};
        T data{};
    };

    Slot slots_[2]{};
    alignas(64) std::atomic<uint32_t> published_{0};
    uint32_t consumed_{0};
};

} // namespace agentic_synth::jsi
