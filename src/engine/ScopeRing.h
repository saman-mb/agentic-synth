#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace agentic_synth {

// ---------------------------------------------------------------------------
// ScopeRing — lock-free stereo visualizer ring (#434/#435/#436).
//
// Why not SPSCQueue<float, N>? The scope feeds an FFT, and an FFT over a
// window that straddles the queue's element boundary — or over a producer-
// overrun gap — produces garbage: the trace looks continuous but the spectrum
// is a smear. ScopeRing stores interleaved stereo frames and hands the
// consumer a *contiguous* window of the newest `frames` frames, or nothing.
//
// Contiguity is enforced per frame with a seqlock, not a single post-copy
// index re-read. The producer stamps each slot odd before writing and even
// after; the consumer validates every slot before and after reading it. A
// window whose producer overran it mid-copy therefore fails validation and is
// reported stale (return 0) instead of being drawn or FFT'd across the
// discontinuity. This closes the race where a slot write is visible to the
// consumer before the producer has published the advanced write index.
//
// Guarantees:
//   - No dynamic allocation after construction, no locks, no syscalls.
//   - Exactly one producer thread (audio thread, push()).
//   - Exactly one consumer thread (message thread, pullLatest()/reset()).
//   - Capacity is a power of two.
// ---------------------------------------------------------------------------

class ScopeRing {
public:
    static constexpr std::size_t kChannels = 2;          // interleaved L/R
    static constexpr std::size_t kCapacityFrames = 4096; // ~85 ms @ 48 kHz
    static_assert((kCapacityFrames & (kCapacityFrames - 1)) == 0, "capacity must be a power of two");

    ScopeRing() noexcept = default;

    ScopeRing(const ScopeRing&) = delete;
    ScopeRing& operator=(const ScopeRing&) = delete;
    ScopeRing(ScopeRing&&) = delete;
    ScopeRing& operator=(ScopeRing&&) = delete;

    // -----------------------------------------------------------------------
    // push — called ONLY by the audio thread. Appends one interleaved frame.
    // Wait-free: two relaxed loads, two float stores, two release seq stores
    // and one release index store. No allocation, no lock.
    // -----------------------------------------------------------------------
    void push(float left, float right) noexcept {
        const std::size_t w = writeIndex_.load(std::memory_order_relaxed);
        const std::size_t slot = w & kMask;

        // Odd stamp = "slot being written"; consumers reject it.
        seq_[slot].store(w * 2 + 1, std::memory_order_release);
        const std::size_t base = slot * kChannels;
        storage_[base] = left;
        storage_[base + 1] = right;
        // Even stamp = "frame w complete"; consumers accept exactly this value.
        seq_[slot].store(w * 2 + 2, std::memory_order_release);
        writeIndex_.store(w + 1, std::memory_order_release);
    }

    // -----------------------------------------------------------------------
    // pullLatest — called ONLY by the consumer thread. Copies the newest
    // `frames` interleaved frames into `dest` (which must hold
    // frames * kChannels floats). Returns the number of frames copied, or 0
    // when there is not yet enough history or any frame in the window was
    // overwritten mid-copy (stale). A 0 return must be treated as "skip this
    // window".
    // -----------------------------------------------------------------------
    [[nodiscard]] std::size_t pullLatest(float* dest, std::size_t frames) noexcept {
        if (dest == nullptr || frames == 0 || frames > kCapacityFrames)
            return 0;

        const std::size_t end = writeIndex_.load(std::memory_order_acquire);
        if (end < frames) {
            lastPullStale_ = false;
            return 0; // not enough history yet
        }
        const std::size_t start = end - frames;

        for (std::size_t i = 0; i < frames; ++i) {
            const std::size_t frame = start + i;
            const std::size_t slot = frame & kMask;
            const std::size_t expected = frame * 2 + 2; // even "complete" stamp

            const std::size_t before = seq_[slot].load(std::memory_order_acquire);
            if (before != expected) {
                // Slot is mid-write (odd) or already holds a newer lap.
                markStale(start);
                return 0;
            }
            const std::size_t base = slot * kChannels;
            dest[i * kChannels] = storage_[base];
            dest[i * kChannels + 1] = storage_[base + 1];
            const std::size_t after = seq_[slot].load(std::memory_order_acquire);
            if (after != expected) {
                // Producer overwrote this slot while we were reading it.
                markStale(start);
                return 0;
            }
        }

        accountGap(start);
        lastDeliveredEnd_ = end;
        lastPullStale_ = false;
        return frames;
    }

    // Consumer-thread diagnostic: frames the producer overwrote before the
    // consumer could deliver them (gaps between consecutive pulls).
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept {
        return droppedFrames_.load(std::memory_order_relaxed);
    }

    // Consumer-thread diagnostic: pull requests rejected as stale because a
    // slot in the requested window was overwritten mid-copy.
    [[nodiscard]] std::uint64_t staleWindows() const noexcept { return staleWindows_.load(std::memory_order_relaxed); }

    // Total frames ever pushed (monotonic; resets to 0).
    [[nodiscard]] std::uint64_t totalFrames() const noexcept {
        return static_cast<std::uint64_t>(writeIndex_.load(std::memory_order_acquire));
    }

    // True when the most recent pullLatest() rejected an overrun window.
    // Consumer-thread only; "not enough history" leaves it false.
    [[nodiscard]] bool lastPullStale() const noexcept { return lastPullStale_; }

    // Consumer-thread only, while the producer is stopped (prepareToPlay /
    // releaseResources). Clears the ring and all counters.
    void reset() noexcept {
        for (auto& s : seq_)
            s.store(0, std::memory_order_relaxed);
        writeIndex_.store(0, std::memory_order_relaxed);
        droppedFrames_.store(0, std::memory_order_relaxed);
        staleWindows_.store(0, std::memory_order_relaxed);
        lastDeliveredEnd_ = 0;
        lastPullStale_ = false;
    }

private:
    static constexpr std::size_t kMask = kCapacityFrames - 1;

    // Count frames between the last delivered window and this one that were
    // never handed to the consumer. Consumer-only state; no atomics needed.
    void accountGap(std::size_t start) noexcept {
        if (start > lastDeliveredEnd_)
            droppedFrames_.fetch_add(start - lastDeliveredEnd_, std::memory_order_relaxed);
    }

    void markStale(std::size_t start) noexcept {
        accountGap(start);
        staleWindows_.fetch_add(1, std::memory_order_relaxed);
        // The producer is ahead of us now; resume from its current head so the
        // next successful pull does not count the same gap twice.
        lastDeliveredEnd_ = writeIndex_.load(std::memory_order_acquire);
        lastPullStale_ = true;
    }

    alignas(64) std::atomic<std::size_t> writeIndex_{0};
    alignas(64) float storage_[kCapacityFrames * kChannels]{};
    alignas(64) std::atomic<std::size_t> seq_[kCapacityFrames]{};
    alignas(64) std::atomic<std::uint64_t> droppedFrames_{0};
    alignas(64) std::atomic<std::uint64_t> staleWindows_{0};

    std::size_t lastDeliveredEnd_{0}; // consumer-only
    bool lastPullStale_{false};       // consumer-only
};

} // namespace agentic_synth
