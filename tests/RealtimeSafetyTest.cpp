#include "engine/RealtimeSafety.h"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace agentic_synth::engine;

TEST_CASE("RealtimeSafety: LockFreeRingBuffer basic push/pop", "[safety]") {
    LockFreeRingBuffer<int, 8> buf;
    int val = 0;

    REQUIRE_FALSE(buf.pop(val)); // empty

    REQUIRE(buf.push(42));
    REQUIRE(buf.pop(val));
    REQUIRE(val == 42);

    REQUIRE_FALSE(buf.pop(val)); // empty again
}

TEST_CASE("RealtimeSafety: LockFreeRingBuffer wraps correctly", "[safety]") {
    LockFreeRingBuffer<int, 4> buf;

    REQUIRE(buf.push(1));
    REQUIRE(buf.push(2));
    REQUIRE(buf.push(3));
    REQUIRE(buf.push(4)); // full
    REQUIRE_FALSE(buf.push(5));

    int val = 0;
    REQUIRE(buf.pop(val));
    REQUIRE(val == 1);
    REQUIRE(buf.pop(val));
    REQUIRE(val == 2);
    REQUIRE(buf.pop(val));
    REQUIRE(val == 3);
    REQUIRE(buf.pop(val));
    REQUIRE(val == 4);
    REQUIRE_FALSE(buf.pop(val));
}

TEST_CASE("RealtimeSafety: ScopedRealtimeContext sets flag", "[safety]") {
    REQUIRE_FALSE(isRealtimeContext().load());

    {
        ScopedRealtimeContext ctx;
        REQUIRE(isRealtimeContext().load());
    }

    REQUIRE_FALSE(isRealtimeContext().load());
}

TEST_CASE("RealtimeSafety: LockFreeRingBuffer concurrent producer/consumer stress test", "[safety][concurrent]") {
    LockFreeRingBuffer<int, 1024> buf;
    constexpr int kIterations = 100'000;
    std::atomic<int> consumed{0};

    std::thread producer([&]() {
        for (int i = 0; i < kIterations; ++i) {
            while (!buf.push(i)) {
                // spin — buffer full, wait for consumer
            }
        }
    });

    std::thread consumer([&]() {
        int val = 0;
        for (int i = 0; i < kIterations; ++i) {
            while (!buf.pop(val)) {
                // spin — buffer empty, wait for producer
            }
            consumed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(consumed.load() == kIterations);
}
