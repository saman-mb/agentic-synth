#include "engine/SPSCQueue.h"

#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

using namespace agentic_synth;

// Use a small integer type for fast test iteration
using TestQueue = SPSCQueue<int, 4>;

TEST_CASE("SPSCQueue: starts empty", "[spsc]") {
    TestQueue q;
    REQUIRE(q.empty());
    REQUIRE(q.size_approx() == 0);
    REQUIRE_FALSE(q.pop().has_value());
}

TEST_CASE("SPSCQueue: push then pop returns same value in FIFO order", "[spsc]") {
    TestQueue q;

    REQUIRE(q.push(10));
    REQUIRE(q.push(20));
    REQUIRE(q.push(30));

    REQUIRE(q.size_approx() == 3);

    auto v1 = q.pop();
    REQUIRE(v1.has_value());
    REQUIRE(*v1 == 10);

    auto v2 = q.pop();
    REQUIRE(v2.has_value());
    REQUIRE(*v2 == 20);

    auto v3 = q.pop();
    REQUIRE(v3.has_value());
    REQUIRE(*v3 == 30);

    REQUIRE(q.empty());
}

TEST_CASE("SPSCQueue: returns false when full", "[spsc]") {
    TestQueue q; // capacity = 4

    REQUIRE(q.push(1));
    REQUIRE(q.push(2));
    REQUIRE(q.push(3));
    REQUIRE(q.push(4));
    // At capacity - next push should fail
    REQUIRE_FALSE(q.push(5));
}

TEST_CASE("SPSCQueue: drain_latest discards intermediate items", "[spsc]") {
    TestQueue q;

    REQUIRE(q.push(1));
    REQUIRE(q.push(2));
    REQUIRE(q.push(3));

    auto latest = q.drain_latest();
    REQUIRE(latest.has_value());
    REQUIRE(*latest == 3);

    // Queue should now be empty (all items drained)
    REQUIRE(q.empty());
}

TEST_CASE("SPSCQueue: drain_latest on empty queue returns nullopt", "[spsc]") {
    TestQueue q;
    auto result = q.drain_latest();
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("SPSCQueue: wrap-around preserves FIFO order", "[spsc]") {
    // Fill, drain one, then add more to test wrap-around
    TestQueue q;

    REQUIRE(q.push(1));
    REQUIRE(q.push(2));
    REQUIRE(q.push(3));

    // Pop one to make room
    auto v = q.pop();
    REQUIRE(v.has_value());
    REQUIRE(*v == 1);

    // Push another — this will wrap in the ring buffer
    REQUIRE(q.push(4));

    // Read remaining in order
    v = q.pop();
    REQUIRE(v.has_value());
    REQUIRE(*v == 2);

    v = q.pop();
    REQUIRE(v.has_value());
    REQUIRE(*v == 3);

    v = q.pop();
    REQUIRE(v.has_value());
    REQUIRE(*v == 4);

    REQUIRE(q.empty());
}

TEST_CASE("SPSCQueue: PatchStruct push/pop round-trip", "[spsc]") {
    SPSCQueue<PatchStruct, 8> q;
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 1234.0f;
    p.master_gain = 0.5f;

    REQUIRE(q.push(p));

    auto result = q.pop();
    REQUIRE(result.has_value());
    REQUIRE(result->filter.cutoff_hz == 1234.0f);
    REQUIRE(result->master_gain == 0.5f);
}

TEST_CASE("SPSCQueue: size_approx after mixed add/remove", "[spsc]") {
    TestQueue q;

    REQUIRE(q.size_approx() == 0);

    q.push(10);
    q.push(20);
    REQUIRE(q.size_approx() == 2);

    q.pop();
    REQUIRE(q.size_approx() == 1);

    q.pop();
    REQUIRE(q.size_approx() == 0);
}
