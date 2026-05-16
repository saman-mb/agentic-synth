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

#include <cstring>

#include "agent/AgentBridge.h"
#include "agent/ParamMap.h"
#include "engine/PatchStruct.h"

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
    // which is wired in the ctor to also call notifyPatch. Phase 18+ patchToVar
    // emits the full nested PatchStruct shape (matching React PatchParams),
    // so cutoff lives at data.filter.cutoff_hz (snake_case nested).
    bridge.feedChunk(R"({"filter": {"cutoff_hz": 1234.0}})");

    REQUIRE(drainUntil([&] { return !patches.empty(); }));
    REQUIRE(patches[0].isObject());
    const auto& data = patches[0]["data"];
    REQUIRE(data.isObject());
    const auto& filter = data["filter"];
    REQUIRE(filter.isObject());
    CHECK(static_cast<double>(filter["cutoff_hz"]) == Catch::Approx(1234.0).epsilon(0.1));
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

TEST_CASE("ParamMap: voice_count routes through paramToDelta with rounding cast") {
    // Phase 18+: voice_count became reachable via the agent's wire path so
    // generated patches can pick polyphony. Cast policy is round-to-nearest
    // (8.7f → 9). The previous contract ("unreachable") is now obsolete.
    const auto d = agent::paramToDelta("voice_count", 8.7f);
    REQUIRE(d.voice_count.has_value());
    CHECK(static_cast<int>(*d.voice_count) == 9);
    // Dotted "global.voice_count" remains unreachable — only the bare key
    // is wired; the dotted form is preserved as a "not in this table" canary.
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

TEST_CASE("AgentBridge::patchToVar / patchFromVar round-trip preserves PatchStruct") {
    // The UI receives the full nested PatchStruct shape on 'patch' and
    // 'patch_update' frames. Round-trip identity (within float tolerance)
    // is the contract that lets feedback frames and tests serialise patches
    // through the wire and recover them exactly. Tweaks below cover every
    // module group so future shape changes have to be reflected in both
    // directions.
    PatchStruct in = make_default_patch();
    in.osc[0].type = OscType::Wavetable;
    in.osc[0].volume = 0.73f;
    in.osc[0].detune_cents = -12.5f;
    in.osc[0].semitone_offset = -7.0f;
    in.osc[0].wavetable_pos = 0.42f;
    in.osc[0].fm_ratio = 2.5f;
    in.osc[0].fm_depth = 0.6f;
    in.osc[0].pulse_width = 0.31f;
    in.osc[0].pan = -0.4f;
    in.osc[0].enabled = 1u;
    in.osc[1].type = OscType::FM;
    in.osc[1].enabled = 1u;
    in.osc[1].volume = 0.4f;
    in.osc[2].enabled = 0u;
    in.osc[2].volume = 0.0f;

    in.filter.type = FilterType::BandPass;
    in.filter.cutoff_hz = 1234.5f;
    in.filter.resonance = 0.66f;
    in.filter.env_mod = -0.4f;
    in.filter.key_track = 0.25f;
    in.filter.drive = 0.55f;

    in.amp_env = {0.123f, 0.456f, 0.7f, 0.89f};
    in.filter_env = {0.05f, 0.2f, 0.3f, 0.4f};

    in.lfo[0].waveform = LfoWaveform::Sawtooth;
    in.lfo[0].target = LfoTarget::FilterCutoff;
    in.lfo[0].rate_hz = 3.14f;
    in.lfo[0].depth = 0.42f;
    in.lfo[0].phase_offset = 0.25f;
    in.lfo[0].bpm_sync = 1u;
    in.lfo[1].waveform = LfoWaveform::Triangle;
    in.lfo[1].target = LfoTarget::Pan;
    in.lfo[1].rate_hz = 0.5f;
    in.lfo[1].depth = 0.15f;

    in.reverb = {0.82f, 0.45f, 1.0f, 0.33f};
    in.delay.time_s = 0.625f;
    in.delay.feedback = 0.45f;
    in.delay.mix = 0.22f;
    in.delay.stereo = 0.7f;
    in.delay.bpm_sync = 1u;

    in.master_gain = 0.78f;
    in.portamento_s = 0.18f;
    in.voice_count = 6;

    const juce::var wire = AgentBridge::patchToVar(in);
    const PatchStruct out = AgentBridge::patchFromVar(wire);

    constexpr float kTol = 1e-4f;
    for (int i = 0; i < kMaxOscillators; ++i) {
        CHECK(out.osc[i].type == in.osc[i].type);
        CHECK(out.osc[i].volume == Catch::Approx(in.osc[i].volume).epsilon(kTol));
        CHECK(out.osc[i].detune_cents == Catch::Approx(in.osc[i].detune_cents).epsilon(kTol));
        CHECK(out.osc[i].semitone_offset == Catch::Approx(in.osc[i].semitone_offset).epsilon(kTol));
        CHECK(out.osc[i].wavetable_pos == Catch::Approx(in.osc[i].wavetable_pos).epsilon(kTol));
        CHECK(out.osc[i].fm_ratio == Catch::Approx(in.osc[i].fm_ratio).epsilon(kTol));
        CHECK(out.osc[i].fm_depth == Catch::Approx(in.osc[i].fm_depth).epsilon(kTol));
        CHECK(out.osc[i].pulse_width == Catch::Approx(in.osc[i].pulse_width).epsilon(kTol));
        CHECK(out.osc[i].pan == Catch::Approx(in.osc[i].pan).epsilon(kTol));
        CHECK(out.osc[i].enabled == in.osc[i].enabled);
    }
    CHECK(out.filter.type == in.filter.type);
    CHECK(out.filter.cutoff_hz == Catch::Approx(in.filter.cutoff_hz).epsilon(kTol));
    CHECK(out.filter.resonance == Catch::Approx(in.filter.resonance).epsilon(kTol));
    CHECK(out.filter.env_mod == Catch::Approx(in.filter.env_mod).epsilon(kTol));
    CHECK(out.filter.key_track == Catch::Approx(in.filter.key_track).epsilon(kTol));
    CHECK(out.filter.drive == Catch::Approx(in.filter.drive).epsilon(kTol));
    CHECK(out.amp_env.attack_s == Catch::Approx(in.amp_env.attack_s).epsilon(kTol));
    CHECK(out.amp_env.sustain == Catch::Approx(in.amp_env.sustain).epsilon(kTol));
    CHECK(out.filter_env.release_s == Catch::Approx(in.filter_env.release_s).epsilon(kTol));
    for (int i = 0; i < kMaxLfos; ++i) {
        CHECK(out.lfo[i].waveform == in.lfo[i].waveform);
        CHECK(out.lfo[i].target == in.lfo[i].target);
        CHECK(out.lfo[i].rate_hz == Catch::Approx(in.lfo[i].rate_hz).epsilon(kTol));
        CHECK(out.lfo[i].depth == Catch::Approx(in.lfo[i].depth).epsilon(kTol));
        CHECK(out.lfo[i].phase_offset == Catch::Approx(in.lfo[i].phase_offset).epsilon(kTol));
        CHECK(out.lfo[i].bpm_sync == in.lfo[i].bpm_sync);
    }
    CHECK(out.reverb.mix == Catch::Approx(in.reverb.mix).epsilon(kTol));
    CHECK(out.reverb.size == Catch::Approx(in.reverb.size).epsilon(kTol));
    CHECK(out.delay.time_s == Catch::Approx(in.delay.time_s).epsilon(kTol));
    CHECK(out.delay.stereo == Catch::Approx(in.delay.stereo).epsilon(kTol));
    CHECK(out.delay.feedback == Catch::Approx(in.delay.feedback).epsilon(kTol));
    CHECK(out.delay.bpm_sync == in.delay.bpm_sync);
    CHECK(out.master_gain == Catch::Approx(in.master_gain).epsilon(kTol));
    CHECK(out.portamento_s == Catch::Approx(in.portamento_s).epsilon(kTol));
    CHECK(static_cast<int>(out.voice_count) == static_cast<int>(in.voice_count));
}

TEST_CASE("AgentBridge::modulationPlanForPatch emits four named macros with routes") {
    // The UI relies on the host-derived modulation plan to label the four
    // macro knobs and pre-fill the mod matrix. Contract: always four macros,
    // each with a non-empty name and at least one route. Heuristic branches
    // (dark/bright, motion, fm/wavetable/spread, space) are exercised via two
    // representative patches.
    auto checkPlan = [](const PatchStruct& patch) {
        const juce::var plan = AgentBridge::modulationPlanForPatch(patch);
        REQUIRE(plan.isObject());
        const auto& macros = plan["macros"];
        const auto* arr = macros.getArray();
        REQUIRE(arr != nullptr);
        REQUIRE(arr->size() == 4);
        for (const auto& macro : *arr) {
            REQUIRE(macro.isObject());
            CHECK(!macro["name"].toString().isEmpty());
            const auto* routes = macro["routes"].getArray();
            REQUIRE(routes != nullptr);
            REQUIRE(routes->size() >= 1);
            for (const auto& route : *routes) {
                CHECK(!route["target"].toString().isEmpty());
                const double amount = static_cast<double>(route["amount"]);
                CHECK(std::abs(amount) <= 1.0);
            }
        }
    };

    // Dark FM patch: should pick GRIP + EDGE branches.
    PatchStruct dark = make_default_patch();
    dark.filter.cutoff_hz = 500.0f;
    dark.osc[0].type = OscType::FM;
    dark.osc[0].fm_depth = 0.8f;
    dark.lfo[0].depth = 0.4f;
    checkPlan(dark);

    // Bright wavetable patch: should pick BRIGHTNESS + MORPH branches.
    PatchStruct bright = make_default_patch();
    bright.filter.cutoff_hz = 8000.0f;
    bright.osc[0].type = OscType::Wavetable;
    bright.osc[0].wavetable_pos = 0.6f;
    bright.reverb.mix = 0.4f;
    checkPlan(bright);
}

// ── Phase 21: LLM-authored rationale priority ────────────────────────────────
//
// generateRationale prefers patch.rationale (LLM-emitted prose) over the
// heuristic template. Empty rationale → heuristic fallback fires.

TEST_CASE("AgentBridge: generateRationale returns LLM-authored prose when patch.rationale is set") {
    JuceFixture fix;
    AgentBridge bridge;

    PatchStruct patch = make_default_patch();
    const char* prose = "I heard: warm, evolving. Three triangles detuned wide, slow filter drift, hall reverb.";
    std::strncpy(patch.rationale, prose, sizeof(patch.rationale) - 1);
    patch.rationale[sizeof(patch.rationale) - 1] = '\0';

    const std::string out = bridge.generateRationale("warm evolving pad", patch);
    CHECK(out == prose);
    // Sanity: confirm the heuristic boilerplate did NOT bleed through.
    CHECK(out.find("I chose a") == std::string::npos);
}

TEST_CASE("AgentBridge: generateRationale falls back to heuristic when patch.rationale is empty") {
    JuceFixture fix;
    AgentBridge bridge;

    PatchStruct patch = make_default_patch();
    // make_default_patch zero-inits the rationale buffer, so rationale[0] == 0.
    REQUIRE(patch.rationale[0] == '\0');

    const std::string out = bridge.generateRationale("warm pad", patch);
    // Phase 30: heuristic was rewritten to lead with the sonic result, not
    // engineer-first "I chose a [type] oscillator" claims. The Phase 30
    // brand-guardian audit named "I chose a" as a banned opener because it
    // (a) leaks parameter-naming into rationale and (b) describes osc[0]
    // only, lying about the full layered topology. Verify the new heuristic
    // produces output AND does NOT use that opener.
    CHECK_FALSE(out.empty());
    CHECK(out.find("I chose a") == std::string::npos);
}

