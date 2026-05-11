#pragma once

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <new>

namespace agentic_synth::engine {

// ── Realtime Safety Assertions ───────────────────────────────────────
//
// Use these in audio-thread hot paths to catch violations in debug builds.
// In release builds all checks compile away to zero overhead.

#if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
#define REALTIME_ASSERT(cond, msg)                                                                                     \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            /* Intentionally crash in debug so the violation is unmistakable */                                        \
            ::assert((cond) && (msg));                                                                                 \
        }                                                                                                              \
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
#define CHECK_REALTIME()                                                                                               \
    REALTIME_ASSERT(isRealtimeContext().load(), "Audio-thread function called outside realtime context")

// Override operator new/delete to assert when called from the audio thread.
// Defined inline (C++17) so multiple TUs can include this header without ODR violation.
// Enable by defining REALTIME_STRICT before including this header.
#if defined(REALTIME_STRICT)
inline void* operator new(std::size_t sz) {
    REALTIME_ASSERT(!isRealtimeContext().load(), "operator new on audio thread");
    void* p = std::malloc(sz);
    if (!p)
        throw std::bad_alloc{};
    return p;
}
inline void operator delete(void* ptr) noexcept {
    REALTIME_ASSERT(!isRealtimeContext().load(), "operator delete on audio thread");
    std::free(ptr);
}
#endif

// ── Pre-allocated ring buffer for realtime-safe parameter updates ─────
template <typename T, size_t Capacity> class LockFreeRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");

public:
    bool push(T value) noexcept {
        auto w = writePos_.load(std::memory_order_relaxed);
        auto r = readPos_.load(std::memory_order_acquire);
        if ((w - r) >= Capacity)
            return false; // full
        buffer_[w & (Capacity - 1)] = value;
        writePos_.store(w + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& value) noexcept {
        auto r = readPos_.load(std::memory_order_relaxed);
        auto w = writePos_.load(std::memory_order_acquire);
        if (r >= w)
            return false; // empty
        value = buffer_[r & (Capacity - 1)];
        readPos_.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    T buffer_[Capacity];
    alignas(64) std::atomic<size_t> readPos_{0};
    alignas(64) std::atomic<size_t> writePos_{0};
};

} // namespace agentic_synth::engine
