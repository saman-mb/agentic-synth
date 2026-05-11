// Phase 2 verification — typed callback API on AgentBridge.
//
// Confirms that:
//   - Each on*() method registers a callback that fires on its notify*().
//   - Payloads round-trip through juce::var with the expected fields.
//   - SubscriberHandle destruction unsubscribes the callback.
//   - Live emission from feedChunk() reaches onPatch subscribers.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "agent/AgentBridge.h"

using namespace agentic_synth;
using namespace agentic_synth::agent;

namespace {

// JUCE message manager is required for callAsync dispatch.  Construct one
// per test via a local scoped object so tests stay isolated.
struct JuceFixture {
    JuceFixture() = default;
    juce::ScopedJuceInitialiser_GUI gui;
};

// Spin the message queue until either the expected count is reached or the
// timeout elapses.  Returns true if the predicate became satisfied.
template <typename Pred>
bool drainUntil(Pred pred, int timeoutMs = 200) {
    auto* mm = juce::MessageManager::getInstance();
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)timeoutMs;
    while (juce::Time::getMillisecondCounter() < deadline) {
        mm->runDispatchLoopUntil(10);
        if (pred())
            return true;
    }
    return pred();
}

} // namespace

TEST_CASE("AgentBridge: onToken receives notifyToken payloads") {
    JuceFixture fix;
    AgentBridge bridge;

    std::vector<juce::var> received;
    auto handle = bridge.onToken([&](const juce::var& v) { received.push_back(v); });

    auto* obj = new juce::DynamicObject{};
    obj->setProperty("content", juce::String("hel"));
    bridge.notifyToken(juce::var{obj});

    REQUIRE(drainUntil([&] { return received.size() == 1; }));
    CHECK(received[0]["content"].toString() == "hel");
}

TEST_CASE("AgentBridge: onPatch / onDone / onError each fire") {
    JuceFixture fix;
    AgentBridge bridge;

    std::vector<juce::var> patches, dones, errors;
    auto h1 = bridge.onPatch([&](const juce::var& v) { patches.push_back(v); });
    auto h2 = bridge.onDone([&](const juce::var& v) { dones.push_back(v); });
    auto h3 = bridge.onError([&](const juce::var& v) { errors.push_back(v); });

    {
        auto* p = new juce::DynamicObject{};
        p->setProperty("variation", 2);
        bridge.notifyPatch(juce::var{p});
    }
    bridge.notifyDone(juce::var{new juce::DynamicObject{}});
    {
        auto* e = new juce::DynamicObject{};
        e->setProperty("message", juce::String("boom"));
        bridge.notifyError(juce::var{e});
    }

    REQUIRE(drainUntil([&] { return patches.size() == 1 && dones.size() == 1 && errors.size() == 1; }));
    CHECK(static_cast<int>(patches[0]["variation"]) == 2);
    CHECK(errors[0]["message"].toString() == "boom");
}

TEST_CASE("AgentBridge: onRationale / onSuggestVariations / onPatchUpdate / onTranscript fire") {
    JuceFixture fix;
    AgentBridge bridge;

    juce::var rationale, suggest, update, transcript;
    auto h1 = bridge.onRationale([&](const juce::var& v) { rationale = v; });
    auto h2 = bridge.onSuggestVariations([&](const juce::var& v) { suggest = v; });
    auto h3 = bridge.onPatchUpdate([&](const juce::var& v) { update = v; });
    auto h4 = bridge.onTranscript([&](const juce::var& v) { transcript = v; });

    auto makeWith = [](const char* k, const char* val) {
        auto* o = new juce::DynamicObject{};
        o->setProperty(juce::Identifier{k}, juce::String(val));
        return juce::var{o};
    };

    bridge.notifyRationale(makeWith("text", "because reasons"));
    bridge.notifySuggestVariations(makeWith("kind", "darker"));
    bridge.notifyPatchUpdate(makeWith("param", "filter.cutoff_hz"));
    bridge.notifyTranscript(makeWith("text", "hello world"));

    REQUIRE(drainUntil([&] {
        return rationale.isObject() && suggest.isObject() && update.isObject() && transcript.isObject();
    }));
    CHECK(rationale["text"].toString() == "because reasons");
    CHECK(suggest["kind"].toString() == "darker");
    CHECK(update["param"].toString() == "filter.cutoff_hz");
    CHECK(transcript["text"].toString() == "hello world");
}

TEST_CASE("AgentBridge: SubscriberHandle destruction unsubscribes") {
    JuceFixture fix;
    AgentBridge bridge;

    std::atomic<int> calls{0};

    {
        auto handle = bridge.onToken([&](const juce::var&) { calls.fetch_add(1, std::memory_order_relaxed); });

        auto* obj = new juce::DynamicObject{};
        obj->setProperty("content", juce::String("first"));
        bridge.notifyToken(juce::var{obj});

        REQUIRE(drainUntil([&] { return calls.load() == 1; }));
    }
    // handle is destroyed — the slot's weak_ptr must now be expired.

    auto* obj2 = new juce::DynamicObject{};
    obj2->setProperty("content", juce::String("second"));
    bridge.notifyToken(juce::var{obj2});

    // Drain anyway so any incorrectly-queued callAsync gets flushed.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(calls.load() == 1);
}

TEST_CASE("AgentBridge: feedChunk emits onPatch via the streaming pipeline") {
    JuceFixture fix;
    AgentBridge bridge;

    std::vector<juce::var> patches;
    auto handle = bridge.onPatch([&](const juce::var& v) { patches.push_back(v); });

    // A single completed top-level field triggers the StreamParser callback,
    // which is wired in the ctor to also call notifyPatch. Phase 4 patchToVar
    // emits React-shaped PatchPreviewData fields (camelCase), so assert
    // against the wire-shape the UI receives.
    bridge.feedChunk(R"({"filter": {"cutoff_hz": 1234.0}})");

    REQUIRE(drainUntil([&] { return !patches.empty(); }));
    REQUIRE(patches[0].isObject());
    const auto& data = patches[0]["data"];
    REQUIRE(data.isObject());
    CHECK(static_cast<double>(data["cutoffHz"]) == Catch::Approx(1234.0).epsilon(0.1));
}

TEST_CASE("AgentBridge: multiple subscribers on the same event all fire") {
    JuceFixture fix;
    AgentBridge bridge;

    int a = 0, b = 0;
    auto h1 = bridge.onDone([&](const juce::var&) { ++a; });
    auto h2 = bridge.onDone([&](const juce::var&) { ++b; });

    bridge.notifyDone(juce::var{new juce::DynamicObject{}});
    REQUIRE(drainUntil([&] { return a == 1 && b == 1; }));
}

TEST_CASE("AgentBridge: dispatch from audio thread drops, does not allocate") {
    JuceFixture fix;
    AgentBridge bridge;

    std::atomic<int> calls{0};
    auto handle = bridge.onPatch([&](const juce::var&) { calls.fetch_add(1, std::memory_order_relaxed); });

    // Spin a worker thread that stamps itself as the "audio" thread, then
    // emits from that same thread.  The tripwire must short-circuit the
    // dispatch before any allocation / callAsync occurs.
    std::thread audio([&] {
        bridge.markAudioThread();
        auto* obj = new juce::DynamicObject{};
        obj->setProperty("variation", juce::String("A"));
        bridge.notifyPatch(juce::var{obj});
    });
    audio.join();

    // Drain so any (incorrectly-queued) callAsync would have a chance to run.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    CHECK(calls.load() == 0);
    CHECK(bridge.audioThreadDropCount() == 1u);
}
