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
#include "agent/ParamMap.h"

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
template <typename Pred> bool drainUntil(Pred pred, int timeoutMs = 200) {
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

    REQUIRE(drainUntil(
        [&] { return rationale.isObject() && suggest.isObject() && update.isObject() && transcript.isObject(); }));
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

// ── Phase 9C: ParamMap table-driven paramToDelta ─────────────────────────────
//
// The 5-place parameter→delta sync (paramToDelta / StreamParser / validator /
// KnobGrid / applyParamToPatch) has been collapsed on the C++ side to a single
// static table in ParamMap.cpp. These tests pin the contract for that table
// so a future row edit can't silently drop a parameter.

TEST_CASE("ParamMap: representative params populate the right PatchDelta field") {
    using agent::paramToDelta;

    SECTION("filter group") {
        auto d = paramToDelta("filter.cutoff_hz", 1234.5f);
        REQUIRE(d.filter_cutoff.has_value());
        CHECK(*d.filter_cutoff == 1234.5f);
        // Spot-check non-targets stay nullopt (single-field invariant).
        CHECK_FALSE(d.filter_resonance.has_value());
        CHECK_FALSE(d.amp_attack.has_value());
        CHECK_FALSE(d.reverb_mix.has_value());
    }
    SECTION("filter.resonance") {
        auto d = paramToDelta("filter.resonance", 0.42f);
        REQUIRE(d.filter_resonance.has_value());
        CHECK(*d.filter_resonance == 0.42f);
    }
    SECTION("amp_env.attack_s") {
        auto d = paramToDelta("amp_env.attack_s", 0.05f);
        REQUIRE(d.amp_attack.has_value());
        CHECK(*d.amp_attack == 0.05f);
    }
    SECTION("filter_env.release_s") {
        auto d = paramToDelta("filter_env.release_s", 1.5f);
        REQUIRE(d.flt_release.has_value());
        CHECK(*d.flt_release == 1.5f);
    }
    SECTION("lfo.0.depth") {
        auto d = paramToDelta("lfo.0.depth", 0.8f);
        REQUIRE(d.lfo0_depth.has_value());
        CHECK(*d.lfo0_depth == 0.8f);
    }
    SECTION("reverb.mix") {
        auto d = paramToDelta("reverb.mix", 0.3f);
        REQUIRE(d.reverb_mix.has_value());
        CHECK(*d.reverb_mix == 0.3f);
    }
    SECTION("delay.feedback") {
        auto d = paramToDelta("delay.feedback", 0.55f);
        REQUIRE(d.delay_feedback.has_value());
        CHECK(*d.delay_feedback == 0.55f);
    }
    SECTION("master_gain (global)") {
        auto d = paramToDelta("master_gain", 0.9f);
        REQUIRE(d.master_gain.has_value());
        CHECK(*d.master_gain == 0.9f);
    }
    SECTION("osc.0.volume") {
        auto d = paramToDelta("osc.0.volume", 0.65f);
        REQUIRE(d.osc0_volume.has_value());
        CHECK(*d.osc0_volume == 0.65f);
    }
    SECTION("osc.1.detune_cents") {
        auto d = paramToDelta("osc.1.detune_cents", 7.0f);
        REQUIRE(d.osc1_detune.has_value());
        CHECK(*d.osc1_detune == 7.0f);
    }
}

TEST_CASE("ParamMap: unknown param returns a default-constructed delta") {
    // Pre-9C the cascade fell through to `return d;` with everything nullopt.
    // The table-driven version must preserve that behaviour exactly — silent
    // no-op, not an exception, not a partial write.
    auto d = agent::paramToDelta("bogus.field", 42.0f);
    CHECK_FALSE(d.filter_cutoff.has_value());
    CHECK_FALSE(d.filter_resonance.has_value());
    CHECK_FALSE(d.amp_attack.has_value());
    CHECK_FALSE(d.amp_decay.has_value());
    CHECK_FALSE(d.amp_sustain.has_value());
    CHECK_FALSE(d.amp_release.has_value());
    CHECK_FALSE(d.flt_attack.has_value());
    CHECK_FALSE(d.flt_release.has_value());
    CHECK_FALSE(d.lfo0_rate.has_value());
    CHECK_FALSE(d.lfo0_depth.has_value());
    CHECK_FALSE(d.reverb_mix.has_value());
    CHECK_FALSE(d.delay_mix.has_value());
    CHECK_FALSE(d.master_gain.has_value());
    CHECK_FALSE(d.portamento.has_value());
    CHECK_FALSE(d.osc0_volume.has_value());
    CHECK_FALSE(d.osc1_volume.has_value());
    CHECK_FALSE(d.voice_count.has_value());
}

TEST_CASE("ParamMap: empty / malformed osc paths fall through cleanly") {
    // Old cascade had a try/catch around std::stoi for `osc.N.field`. The
    // table flattens those into literal-path rows, so anything that wasn't
    // an exact match in the old code is also unmatched here — no parsing,
    // no surprises.
    CHECK_FALSE(agent::paramToDelta("osc.", 1.0f).osc0_volume.has_value());
    CHECK_FALSE(agent::paramToDelta("osc.0.", 1.0f).osc0_volume.has_value());
    CHECK_FALSE(agent::paramToDelta("osc.2.volume", 1.0f).osc0_volume.has_value());
    CHECK_FALSE(agent::paramToDelta("osc.0.unknown", 1.0f).osc0_volume.has_value());
}

TEST_CASE("ParamMap: int-typed PatchDelta fields are unreachable through paramToDelta") {
    // PatchDelta::voice_count is uint8_t. The prior if/else cascade never
    // exposed it via a UI path, and neither does the table. This test pins
    // that contract so a future "add voice_count row" change has to also
    // pick a cast policy (truncate vs round) deliberately — by failing
    // here first.
    CHECK_FALSE(agent::paramToDelta("voice_count", 8.7f).voice_count.has_value());
    CHECK_FALSE(agent::paramToDelta("global.voice_count", 8.7f).voice_count.has_value());
}

TEST_CASE("ParamMap: table is non-empty and every row has a non-null assign") {
    // Defensive invariant: a row with a null assign would segfault inside
    // paramToDelta. CTAD makes the row count compile-time so an accidental
    // 0-row table would also be caught here.
    const auto map = agent::getParamMap();
    REQUIRE(map.size() > 0);
    for (const auto& slot : map) {
        CHECK(slot.assign != nullptr);
        CHECK_FALSE(slot.path.empty());
    }
}
