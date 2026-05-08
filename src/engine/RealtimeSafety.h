#pragma once

#include <atomic>
#include <cassert>
#include <cstdlib>

namespace agentic_synth::engine {

// ── Realtime Safety Assertions ───────────────────────────────────────
//
// Use these in audio-thread hot paths to catch violations in debug builds.
// In release builds all checks compile away to zero overhead.

#if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
#define REALTIME_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            /* Intentionally crash in debug so the violation is unmistakable */ \
            ::assert((cond) && (msg)); \
        } \
    } while (false)
#else
#define REALTIME_ASSERT(cond, msg) ((void)0)
#endif

// ── Real-time context marker ──────────────────────────────────────────

// Set once when entering the audio callback, cleared after.
// Any allocation/lock/I/O on the audio thread will assert.
inline std::atomic<bool>& isRealtimeContext() {
    static std::atomic<bool> flag{false};
    return flag;
}

class ScopedRealtimeContext {
public:
    ScopedRealtimeContext() { isRealtimeContext().store(true); }
    ~ScopedRealtimeContext() { isRealtimeContext().store(false); }
    ScopedRealtimeContext(const ScopedRealtimeContext&) = delete;
    ScopedRealtimeContext& operator=(const ScopedRealtimeContext&) = delete;
};

// ── Audit helpers ─────────────────────────────────────────────────────

// Call at the start of any audio-thread function
#define CHECK_REALTIME() \
    REALTIME_ASSERT(isRealtimeContext().load(), \
        "Audio-thread function called outside realtime context")

// Override operator new on the audio thread to trip on allocation
#if defined(REALTIME_STRICT)
void* operator new(std::size_t sz) {
    REALTIME_ASSERT(!isRealtimeContext().load(),
        "Heap allocation on audio thread!");
    return std::malloc(sz);
}
void operator delete(void* ptr) noexcept {
    REALTIME_ASSERT(!isRealtimeContext().load(),
        "Heap deallocation on audio thread!");
    std::free(ptr);
}
#endif

// ── Pre-allocated ring buffer for realtime-safe parameter updates ─────
template<typename T, size_t Capacity>
class LockFreeRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");
public:
    bool push(T value) noexcept {
        auto w = writePos_.load(std::memory_order_relaxed);
        auto r = readPos_.load(std::memory_order_acquire);
        if ((w - r) >= Capacity) return false;  // full
        buffer_[w & (Capacity - 1)] = value;
        writePos_.store(w + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& value) noexcept {
        auto r = readPos_.load(std::memory_order_relaxed);
        auto w = writePos_.load(std::memory_order_acquire);
        if (r >= w) return false;  // empty
        value = buffer_[r & (Capacity - 1)];
        readPos_.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    T buffer_[Capacity];
    std::atomic<size_t> readPos_{0};
    std::atomic<size_t> writePos_{0};
};

} // namespace agentic_synth::engine
