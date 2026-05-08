#include <catch2/catch_test_macros.hpp>
#include "engine/RealtimeSafety.h"

using namespace agentic_synth::engine;

TEST_CASE("RealtimeSafety: LockFreeRingBuffer basic push/pop", "[safety]") {
    LockFreeRingBuffer<int, 8> buf;
    int val = 0;

    REQUIRE_FALSE(buf.pop(val));  // empty

    REQUIRE(buf.push(42));
    REQUIRE(buf.pop(val));
    REQUIRE(val == 42);

    REQUIRE_FALSE(buf.pop(val));  // empty again
}

TEST_CASE("RealtimeSafety: LockFreeRingBuffer wraps correctly", "[safety]") {
    LockFreeRingBuffer<int, 4> buf;

    REQUIRE(buf.push(1));
    REQUIRE(buf.push(2));
    REQUIRE(buf.push(3));
    REQUIRE(buf.push(4));  // full
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
