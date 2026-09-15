// #434/#435/#436 — stereo, contiguous scope delivery.
//
// The visualiser FFTs a 1024-frame window; a window that straddles a ring
// wrap or a producer-overrun gap produces a plausible-looking but wrong
// spectrum. These tests pin the ScopeRing contract the plugin and the JS
// bridge rely on: interleaved stereo frames, a contiguous newest window, and
// an explicit stale rejection (never a silently-stitched buffer).

#include "engine/ScopeRing.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>

using namespace agentic_synth;

TEST_CASE("ScopeRing: pull returns 0 until the requested history exists", "[scope][spsc]") {
    ScopeRing ring;
    std::vector<float> dest(2 * 16, 0.0f);

    REQUIRE(ring.pullLatest(dest.data(), 16) == 0);

    for (int i = 0; i < 8; ++i)
        ring.push(static_cast<float>(i), -static_cast<float>(i));

    REQUIRE(ring.pullLatest(dest.data(), 16) == 0);
    REQUIRE(ring.pullLatest(dest.data(), 8) == 8);
}

TEST_CASE("ScopeRing: newest window is contiguous and stereo is preserved", "[scope][spsc]") {
    ScopeRing ring;
    std::vector<float> dest(2 * 64, 0.0f);

    // Ramp: frame f -> L = f, R = 1000 + f.
    for (int f = 0; f < 200; ++f)
        ring.push(static_cast<float>(f), static_cast<float>(1000 + f));

    REQUIRE(ring.pullLatest(dest.data(), 64) == 64);

    // The window must be the newest 64 frames: 136..199, contiguous.
    REQUIRE(dest[0] == 136.0f);
    REQUIRE(dest[1] == 1136.0f);
    for (int i = 0; i < 64; ++i) {
        REQUIRE(dest[static_cast<std::size_t>(i) * 2] == 136.0f + static_cast<float>(i));
        REQUIRE(dest[static_cast<std::size_t>(i) * 2 + 1] == 1136.0f + static_cast<float>(i));
    }
}

TEST_CASE("ScopeRing: wrap-around still yields a contiguous tail", "[scope][spsc]") {
    ScopeRing ring;
    // Push more than one full ring so the storage wraps, pulling small windows
    // so the consumer keeps up.
    std::vector<float> dest(2 * 32, 0.0f);
    float expectedL = 0.0f;

    for (int block = 0; block < 400; ++block) {
        for (int i = 0; i < 64; ++i) {
            ring.push(expectedL, -expectedL);
            expectedL += 1.0f;
        }
        REQUIRE(ring.pullLatest(dest.data(), 32) == 32);
        for (int i = 0; i < 32; ++i) {
            const float first = dest[0];
            REQUIRE(dest[static_cast<std::size_t>(i) * 2] == first + static_cast<float>(i));
        }
    }
}

TEST_CASE("ScopeRing: short/invalid requests never corrupt the output", "[scope][spsc]") {
    ScopeRing ring;
    std::vector<float> dest(2 * 8, 0.0f);
    for (int i = 0; i < 4; ++i)
        ring.push(1.0f, 2.0f);

    REQUIRE(ring.pullLatest(nullptr, 4) == 0);
    REQUIRE(ring.pullLatest(dest.data(), 0) == 0);
    REQUIRE(ring.pullLatest(dest.data(), ScopeRing::kCapacityFrames + 1) == 0);
}

TEST_CASE("ScopeRing: reset clears history and counters", "[scope][spsc]") {
    ScopeRing ring;
    std::vector<float> dest(2 * 4, 0.0f);
    for (int i = 0; i < 4; ++i)
        ring.push(static_cast<float>(i), 0.0f);

    ring.reset();
    REQUIRE(ring.totalFrames() == 0);
    REQUIRE(ring.droppedFrames() == 0);
    REQUIRE(ring.staleWindows() == 0);
    REQUIRE(ring.pullLatest(dest.data(), 4) == 0);
}

// Acceptance #436: "Test feeds a known continuous signal through the scope
// path under induced consumer stalls and asserts the delivered window is
// contiguous." A dedicated producer thread keeps pushing a monotonic ramp
// while the consumer stalls, then pulls. Every delivered window must be a
// contiguous slice of the ramp; any window the producer overran is rejected
// (return 0) and counted stale — never stitched from discontinuous samples.
TEST_CASE("ScopeRing: delivered window stays contiguous under consumer stalls", "[scope][spsc]") {
    ScopeRing ring;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> produced{0};

    std::thread producer([&] {
        std::uint64_t f = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            ring.push(static_cast<float>(f & 0xFFFFFFu), static_cast<float>(f & 0xFFFFFFu));
            ++f;
            produced.store(f, std::memory_order_relaxed);
        }
    });

    // Let the producer build a deep backlog, then stall the consumer well past
    // the ring's capacity to force overrun.
    while (produced.load(std::memory_order_relaxed) < ScopeRing::kCapacityFrames * 8)
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Request the whole ring: with zero slack, any producer advance during the
    // copy is an overrun, so a concurrent producer reliably exercises the
    // stale path instead of depending on copy-vs-push timing luck.
    constexpr std::size_t kFrames = ScopeRing::kCapacityFrames;
    bool sawStale = false;
    std::vector<float> dest(2 * kFrames, 0.0f);
    for (int attempt = 0; attempt < 5000 && !sawStale; ++attempt) {
        const std::size_t got = ring.pullLatest(dest.data(), kFrames);
        if (got == 0) {
            sawStale = ring.staleWindows() > 0;
            continue;
        }
        // Delivered window: contiguous ramp and both channels identical (we
        // pushed L == R for this test).
        const float first = dest[0];
        for (std::size_t i = 0; i < got; ++i) {
            const float expected = first + static_cast<float>(i);
            REQUIRE(dest[i * 2] == expected);
            REQUIRE(dest[i * 2 + 1] == expected);
        }
    }

    stop.store(true);
    producer.join();

    REQUIRE(sawStale);
    REQUIRE(ring.staleWindows() > 0);
    REQUIRE(ring.droppedFrames() > 0);
}
