// Phase 1 P0 ship-blocker verification.
//
// Covers four lifecycle/correctness contracts that previously had no
// direct test:
//
//   1. SPSCQueue<RawMidiMsg> burst push/drain — audition keyboard fan-in
//      replaces the old CriticalSection + MidiBuffer (alloc + lock on RT).
//   2. VoiceManager::releaseResources() hard-clears filter / DC blocker /
//      envelope / FX state. Host releaseResources → prepareToPlay(newSR)
//      must not leak previous-SR Moog integrators or reverb tails.
//   3. VoiceManager::primeSmoothers() snaps cutoff/res/gain smoothers so
//      the first sample after prepareToPlay sits at the target value — no
//      audible glide-up on transport restart or offline bounce.
//   4. Smoothers re-targeted after a state restore (mid-playback patch
//      reload) hit the new target — invalidateParameterShadows + reapply
//      path on AgenticSynthPlugin's APVTS restore.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

#include "engine/MidiHandler.h"
#include "engine/Reverb.h"
#include "engine/SPSCQueue.h"
#include "engine/VoiceManager.h"

#include "plugin/AgenticSynthPlugin.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

using agentic_synth::SPSCQueue;
using agentic_synth::engine::RawMidiMsg;
using agentic_synth::engine::Reverb;
using agentic_synth::engine::VoiceManager;

// ── 1. SPSCQueue audition path correctness ────────────────────────────────────

TEST_CASE("Audition SPSC queue: burst from producer thread drains intact on consumer") {
    // Mirrors the audition wiring used by AgenticSynthPlugin: producer is
    // the JUCE message/Timer thread; consumer is the audio thread draining
    // ≤ kDrainBudget per block. We assert the contract: every message that
    // successfully pushes is delivered in FIFO order; the queue's 256-slot
    // capacity is more than enough for a single burst of 128 (the worst
    // realistic case is a polyphonic audition tap).
    constexpr std::size_t kCap = 256;
    SPSCQueue<RawMidiMsg, kCap> q;

    constexpr int kBurst = 128;
    std::atomic<bool> producerDone{false};

    std::thread producer([&] {
        for (int i = 0; i < kBurst; ++i) {
            const auto msg = (i % 2 == 0) ? RawMidiMsg::noteOn(60 + i % 24, 100)
                                          : RawMidiMsg::noteOff(60 + (i - 1) % 24);
            // Capacity headroom > burst so this must succeed every time.
            REQUIRE(q.push(msg));
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::vector<RawMidiMsg> drained;
    drained.reserve(kBurst);

    // Drain like processBlock would — bounded ≤64 per pass, looping until
    // producer finishes AND the queue empties.
    while (!producerDone.load(std::memory_order_acquire) || !q.empty()) {
        constexpr int kDrainBudget = 64;
        for (int i = 0; i < kDrainBudget; ++i) {
            auto m = q.pop();
            if (!m.has_value())
                break;
            drained.push_back(*m);
        }
        std::this_thread::yield();
    }
    producer.join();

    REQUIRE(drained.size() == kBurst);
    // FIFO order preserved: even index → 0x90 (noteOn), odd → 0x80 (noteOff).
    for (int i = 0; i < kBurst; ++i) {
        const uint8_t expected = (i % 2 == 0) ? 0x90u : 0x80u;
        CHECK((drained[static_cast<std::size_t>(i)].status & 0xF0u) == expected);
    }
}

TEST_CASE("Audition SPSC queue: full-queue push returns false, no UB") {
    // Real fix-5 path drops on full (silent). Verify push() returns false
    // for overflow and the queue continues to function once drained.
    constexpr std::size_t kCap = 4; // smallest power-of-two we can use easily
    SPSCQueue<RawMidiMsg, kCap> q;

    // Ring reserves one slot to distinguish full from empty → capacity-1 = 3.
    CHECK(q.push(RawMidiMsg::noteOn(60, 100)));
    CHECK(q.push(RawMidiMsg::noteOn(61, 100)));
    CHECK(q.push(RawMidiMsg::noteOn(62, 100)));
    CHECK_FALSE(q.push(RawMidiMsg::noteOn(63, 100))); // full → drop

    // Drain one and re-push.
    auto m = q.pop();
    REQUIRE(m.has_value());
    CHECK(m->data1 == 60);
    CHECK(q.push(RawMidiMsg::noteOn(64, 100)));
}

// ── 2. releaseResources clears voice/filter/delay/reverb state ────────────────

namespace {

// Render N samples through the stereo path and return the peak |x| seen.
float renderPeakStereo(VoiceManager& vm, int samples) {
    std::vector<float> l(static_cast<std::size_t>(samples), 0.0f);
    std::vector<float> r(static_cast<std::size_t>(samples), 0.0f);
    vm.renderBlock(l.data(), r.data(), samples);
    float peak = 0.0f;
    for (int i = 0; i < samples; ++i) {
        peak = std::max(peak, std::abs(l[static_cast<std::size_t>(i)]));
        peak = std::max(peak, std::abs(r[static_cast<std::size_t>(i)]));
    }
    return peak;
}

} // namespace

TEST_CASE("VoiceManager::releaseResources kills voice and reverb tail") {
    VoiceManager vm(4);
    vm.prepare(44100.0);

    // Light up a voice + spin up some reverb tail by playing a note.
    agentic_synth::PatchStruct patch = agentic_synth::make_default_patch();
    patch.reverb.size = 1.0f; // long tail
    patch.reverb.damping = 0.0f;
    patch.reverb.mix = 1.0f;
    vm.applyPatch(patch);

    vm.noteOn(60, 1.0f);
    {
        std::vector<float> l(4096, 0.0f);
        std::vector<float> r(4096, 0.0f);
        vm.renderBlock(l.data(), r.data(), 4096);
    }
    vm.noteOff(60);

    // Confirm there *is* audible tail in the pipeline right now.
    const float peakBefore = renderPeakStereo(vm, 256);
    REQUIRE(peakBefore > 1.0e-4f);

    // releaseResources → immediately rendering must produce silence (no
    // reverb tail, no envelope output, no filter ring-out).
    vm.releaseResources();
    const float peakAfter = renderPeakStereo(vm, 256);
    CHECK(peakAfter == 0.0f);

    // No active voices either.
    CHECK(vm.activeVoiceCount() == 0);
    CHECK(vm.activeNotes().empty());
}

// ── 3. primeSmoothers snaps cutoff/res/gain to target ─────────────────────────

TEST_CASE("primeSmoothers snaps smoothers — no first-block glide from defaults") {
    VoiceManager vm(2);
    vm.prepare(48000.0);

    // Target cutoff well away from the 1000 Hz default so any glide would
    // be unambiguously observable.
    vm.setFilterCutoff(8000.0f);
    vm.setFilterResonance(0.6f);
    vm.setMasterGain(0.25f);

    // Without primeSmoothers the smoother starts at the default and creeps
    // up sample-by-sample. With primeSmoothers it should sit exactly on
    // target on the very first read.
    vm.primeSmoothers();
    CHECK_THAT(vm.currentSmoothedCutoff(),
               Catch::Matchers::WithinAbs(8000.0f, 1.0e-3f));
    CHECK_THAT(vm.currentSmoothedGain(),
               Catch::Matchers::WithinAbs(0.25f, 1.0e-6f));

    // Render one sample — value must still match the target within one
    // smoother step (sanity check: primed state means no glide).
    std::vector<float> buf(1, 0.0f);
    vm.renderBlock(buf.data(), 1);
    CHECK_THAT(vm.currentSmoothedCutoff(),
               Catch::Matchers::WithinAbs(8000.0f, 1.0e-3f));
    CHECK_THAT(vm.currentSmoothedGain(),
               Catch::Matchers::WithinAbs(0.25f, 1.0e-6f));
}

// ── 4. setStateInformation-style reapply: targets propagate after invalidate ──

TEST_CASE("Mid-playback retarget + primeSmoothers reflects new params next block") {
    // Emulates AgenticSynthPlugin::setStateInformation: after a state load
    // we invalidate cached shadows, push the freshly-restored values via
    // applyParameters() (effectively setFilterCutoff etc.), then call
    // primeSmoothers() so the next audio block reflects the new state
    // without an audible glide.
    VoiceManager vm(2);
    vm.prepare(44100.0);

    // Initial state.
    vm.setFilterCutoff(2000.0f);
    vm.setMasterGain(1.0f);
    vm.primeSmoothers();

    // Play a note + render a bit, so we are mid-stream when the reload
    // happens.
    vm.noteOn(60, 0.8f);
    std::vector<float> buf(512, 0.0f);
    vm.renderBlock(buf.data(), 512);

    REQUIRE_THAT(vm.currentSmoothedCutoff(),
                 Catch::Matchers::WithinAbs(2000.0f, 1.0f));

    // Reload: new state pushes new targets, smoothers snap to them.
    vm.setFilterCutoff(440.0f);
    vm.setMasterGain(0.5f);
    vm.primeSmoothers();

    // Next read — smoothers must reflect the restored state immediately.
    CHECK_THAT(vm.currentSmoothedCutoff(),
               Catch::Matchers::WithinAbs(440.0f, 1.0e-3f));
    CHECK_THAT(vm.currentSmoothedGain(),
               Catch::Matchers::WithinAbs(0.5f, 1.0e-6f));
}

// ── 5. Plugin-level integration tests (Phase 1 follow-up) ─────────────────────
//
// Code-review/SDET follow-ups: the four tests above all stop at the
// VoiceManager API and emulate the plugin lifecycle. The four tests below
// instantiate AgenticSynthPlugin directly and verify the same contracts at
// the AudioProcessor boundary — drain-budget bound on a single processBlock,
// audition → audio end-to-end, sample-rate change zeroing, and
// getStateInformation/setStateInformation round-trip.

namespace {

// JUCE-friendly fixture: ScopedJuceInitialiser_GUI brings up the
// MessageManager so AudioProcessorValueTreeState (XML parsing, listeners)
// and the AudioProcessor base class behave normally inside the test.
struct PluginFixture {
    juce::ScopedJuceInitialiser_GUI gui;
};

// Render N samples of an empty input through the plugin and return the
// peak |x| across both channels.
float renderPluginPeak(AgenticSynthPlugin& plug, int numSamples) {
    juce::AudioBuffer<float> buf(2, numSamples);
    buf.clear();
    juce::MidiBuffer midi;
    plug.processBlock(buf, midi);
    float peak = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
        const auto* data = buf.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            peak = std::max(peak, std::abs(data[i]));
    }
    return peak;
}

} // namespace

TEST_CASE("AgenticSynthPlugin: single processBlock drains exactly kAuditionDrainBudget") {
    // Stuffs 200 messages into the audition queue from a "producer" (we are
    // on the test thread, which is acting as the message thread). Each
    // message carries a sequence number in data1 so we can verify FIFO
    // ordering across the two-block drain. The plugin guarantees ≤ 64
    // messages are consumed per processBlock — the remainder must survive
    // for the next block.
    PluginFixture fix;
    AgenticSynthPlugin plug;
    plug.prepareToPlay(44100.0, 512);

    auto& q = plug.auditionQueueForTest();
    constexpr int kPushed = 200;
    constexpr int kBudget = AgenticSynthPlugin::auditionDrainBudgetForTest();
    static_assert(kBudget == 64, "test assumes kAuditionDrainBudget == 64");

    // Push 200 alternating noteOn/noteOff with sequence number in data1.
    // noteOff payloads have data2 == 0; we still tag data1 with the index.
    for (int i = 0; i < kPushed; ++i) {
        RawMidiMsg msg;
        if (i % 2 == 0) {
            msg.status = 0x90;
            msg.data1 = static_cast<uint8_t>(i & 0x7F);
            msg.data2 = 100;
        } else {
            msg.status = 0x80;
            msg.data1 = static_cast<uint8_t>(i & 0x7F);
            msg.data2 = 0;
        }
        REQUIRE(q.push(msg));
    }

    // First processBlock — must consume exactly kBudget messages.
    {
        juce::AudioBuffer<float> buf(2, 64);
        buf.clear();
        juce::MidiBuffer midi;
        plug.processBlock(buf, midi);
    }

    // Count remaining by draining into a local vector; then push them back
    // in order so the next processBlock sees them in their original FIFO
    // position. Because pop() removes the head, we drain everything once,
    // record, restore.
    auto snapshotRemaining = [&]() {
        std::vector<RawMidiMsg> remaining;
        while (true) {
            auto m = q.pop();
            if (!m.has_value())
                break;
            remaining.push_back(*m);
        }
        return remaining;
    };

    auto remaining = snapshotRemaining();
    CHECK(remaining.size() == static_cast<std::size_t>(kPushed - kBudget));
    // Sequence numbers must be 64..199 in order.
    for (std::size_t i = 0; i < remaining.size(); ++i) {
        const int expectedSeq = static_cast<int>(i) + kBudget;
        CHECK(static_cast<int>(remaining[i].data1) == (expectedSeq & 0x7F));
    }

    // Restore so the second block can drain.
    for (const auto& m : remaining)
        REQUIRE(q.push(m));

    // Second processBlock — drains the next kBudget.
    {
        juce::AudioBuffer<float> buf(2, 64);
        buf.clear();
        juce::MidiBuffer midi;
        plug.processBlock(buf, midi);
    }

    auto remainingAfterSecond = snapshotRemaining();
    CHECK(remainingAfterSecond.size() == static_cast<std::size_t>(kPushed - 2 * kBudget));
    // FIFO order preserved across both blocks: first survivor's sequence
    // number is 2*kBudget == 128.
    if (!remainingAfterSecond.empty()) {
        const int firstSeqAfterTwoBlocks = 2 * kBudget;
        CHECK(static_cast<int>(remainingAfterSecond.front().data1) == (firstSeqAfterTwoBlocks & 0x7F));
    }
}

TEST_CASE("AgenticSynthPlugin: end-to-end audition → MidiHandler → Voice → audio") {
    // Pushes a noteOn into the audition queue and verifies that one
    // processBlock with an empty input buffer produces non-silent output.
    // Proves the full chain: SPSC drain → MidiHandler::process →
    // VoiceManager::noteOn → renderBlock → buffer. A tiny output threshold
    // (1e-3) is robust against the amp envelope's initial attack ramp.
    PluginFixture fix;
    AgenticSynthPlugin plug;
    plug.prepareToPlay(44100.0, 512);

    auto& q = plug.auditionQueueForTest();
    REQUIRE(q.push(RawMidiMsg::noteOn(60, 100)));

    // Process enough samples for the amp envelope to climb past the
    // 1e-3 threshold. The default attack is 5 ms (~220 samples at 44.1 kHz)
    // so 512 samples is comfortably past peak.
    const float peak = renderPluginPeak(plug, 512);
    CHECK(peak > 1.0e-3f);

    // Drop the audition sink before fixture teardown so any in-flight
    // Timer::callAfterDelay registered by AgentBridge cannot fire into a
    // half-destroyed plugin (mirrors the dtor contract).
    plug.agentBridge().setMidiNoteSink(nullptr);
}

TEST_CASE("AgenticSynthPlugin: prepareToPlay across sample rates zeroes state") {
    // Verifies the prepareToPlay → releaseResources → prepare path inside
    // the plugin. After playing a note at 44.1k and then re-priming the
    // plugin at 48k WITHOUT an explicit releaseResources call, a silent
    // processBlock must yield exactly zero output — proving that the
    // internal releaseResources fires before the new SR coefficients take
    // effect (no leaked Moog integrators / reverb tails / envelope stages).
    PluginFixture fix;
    AgenticSynthPlugin plug;
    plug.prepareToPlay(44100.0, 512);

    // Drive a voice + a few blocks so reverb/delay state is non-zero.
    auto& q = plug.auditionQueueForTest();
    REQUIRE(q.push(RawMidiMsg::noteOn(60, 100)));
    REQUIRE(renderPluginPeak(plug, 512) > 1.0e-4f);
    // Render a few more blocks to populate FX state.
    for (int i = 0; i < 4; ++i)
        (void)renderPluginPeak(plug, 512);

    // SR change — explicitly NO releaseResources call in between, mirroring
    // hosts that skip it. The plugin's own prepareToPlay calls
    // releaseResources before the new coefficients are computed.
    plug.prepareToPlay(48000.0, 512);

    // Silent processBlock — no MIDI in the queue, no held note. Every
    // sample must be exactly zero.
    juce::AudioBuffer<float> buf(2, 256);
    buf.clear();
    juce::MidiBuffer midi;
    plug.processBlock(buf, midi);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
        const auto* data = buf.getReadPointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
            CHECK(data[i] == 0.0f);
    }

    plug.agentBridge().setMidiNoteSink(nullptr);
}

TEST_CASE("AgenticSynthPlugin: getStateInformation/setStateInformation round-trip applies to engine") {
    // Modifies APVTS params on plugin A, serializes via getStateInformation,
    // deserializes into plugin B via setStateInformation, and verifies that
    // B's VoiceManager smoothers reflect the restored values after the next
    // processBlock. This exercises the full invalidateParameterShadows →
    // applyParameters → primeSmoothers chain on the real plugin path.
    PluginFixture fix;
    AgenticSynthPlugin plugA;
    plugA.prepareToPlay(44100.0, 512);

    // Push distinctive values to APVTS so any failure to restore is
    // unambiguous (default cutoff = 5000, gain = 0.8).
    plugA.getAPVTS().getParameter("filterCutoff")->setValueNotifyingHost(
        plugA.getAPVTS().getParameter("filterCutoff")->convertTo0to1(8000.0f));
    plugA.getAPVTS().getParameter("filterResonance")->setValueNotifyingHost(
        plugA.getAPVTS().getParameter("filterResonance")->convertTo0to1(0.7f));
    plugA.getAPVTS().getParameter("masterGain")->setValueNotifyingHost(
        plugA.getAPVTS().getParameter("masterGain")->convertTo0to1(0.5f));

    juce::MemoryBlock state;
    plugA.getStateInformation(state);
    REQUIRE(state.getSize() > 0);

    // Plugin B starts with defaults, then loads plugin A's state.
    AgenticSynthPlugin plugB;
    plugB.prepareToPlay(44100.0, 512);

    plugB.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    // setStateInformation → invalidateParameterShadows → applyParameters →
    // primeSmoothers; the cutoff/gain smoothers must already be snapped to
    // the restored values without any processBlock call.
    CHECK_THAT(plugB.voiceManagerForTest().currentSmoothedCutoff(),
               Catch::Matchers::WithinAbs(8000.0f, 1.0f));
    CHECK_THAT(plugB.voiceManagerForTest().currentSmoothedGain(),
               Catch::Matchers::WithinAbs(0.5f, 1.0e-3f));

    // Render one block with a note — smoothers stay on-target (no glide-up
    // from the constructor defaults).
    REQUIRE(plugB.auditionQueueForTest().push(RawMidiMsg::noteOn(60, 100)));
    (void)renderPluginPeak(plugB, 64);
    CHECK_THAT(plugB.voiceManagerForTest().currentSmoothedCutoff(),
               Catch::Matchers::WithinAbs(8000.0f, 5.0f));
    CHECK_THAT(plugB.voiceManagerForTest().currentSmoothedGain(),
               Catch::Matchers::WithinAbs(0.5f, 1.0e-3f));

    plugA.agentBridge().setMidiNoteSink(nullptr);
    plugB.agentBridge().setMidiNoteSink(nullptr);
}

// ── 6. Phase 2 (Item #2) — APVTS covers every PatchStruct field ──────────────
//
// State recall was broken in Phase 1 because only 8 of ~50 PatchStruct fields
// were exposed via APVTS. Save+reload would default the rest to a sawtooth
// patch. These tests pin the full coverage and verify the new
// buildPatchFromApvts / writePatchToApvts helpers are mutual inverses (modulo
// the float tolerances each NormalisableRange step imposes).

namespace {

// Distinctive non-default values for every PatchStruct field. Picked to stay
// inside the APVTS NormalisableRange of each param so setValueNotifyingHost
// does not clamp silently.
agentic_synth::PatchStruct makeRichTestPatch() {
    using namespace agentic_synth;
    PatchStruct p = make_default_patch();

    p.master_gain = 0.6f;
    p.portamento_s = 0.42f;
    p.voice_count = 12;

    p.filter.type = FilterType::BandPass;
    p.filter.cutoff_hz = 3400.0f;
    p.filter.resonance = 0.55f;
    p.filter.env_mod = -0.35f;
    p.filter.key_track = 0.7f;
    p.filter.drive = 0.4f;

    p.amp_env.attack_s = 0.11f;
    p.amp_env.decay_s = 0.33f;
    p.amp_env.sustain = 0.7f;
    p.amp_env.release_s = 1.5f;

    p.filter_env.attack_s = 0.02f;
    p.filter_env.decay_s = 0.4f;
    p.filter_env.sustain = 0.25f;
    p.filter_env.release_s = 0.8f;

    for (int i = 0; i < kMaxOscillators; ++i) {
        auto& o = p.osc[i];
        o.type = static_cast<OscType>((i + 2) % 8); // not the default Sawtooth/Sine
        o.detune_cents = -7.5f + 5.0f * static_cast<float>(i);
        o.semitone_offset = static_cast<float>(i) * 4.0f;
        o.wavetable_pos = 0.1f * static_cast<float>(i + 1);
        o.fm_ratio = 2.0f + 0.5f * static_cast<float>(i);
        o.fm_depth = 0.2f * static_cast<float>(i + 1);
        o.pulse_width = 0.3f + 0.1f * static_cast<float>(i);
        o.pan = -0.5f + 0.5f * static_cast<float>(i);
        o.volume = 0.4f + 0.1f * static_cast<float>(i);
        o.enabled = (i == 1) ? 0 : 1; // disable the middle one
    }

    for (int i = 0; i < kMaxLfos; ++i) {
        auto& l = p.lfo[i];
        l.waveform = static_cast<LfoWaveform>((i + 1) % 5);
        l.target = static_cast<LfoTarget>((i + 2) % 7);
        l.rate_hz = 0.5f + 1.5f * static_cast<float>(i);
        l.depth = 0.3f * static_cast<float>(i + 1);
        l.phase_offset = 0.25f * static_cast<float>(i + 1);
        l.bpm_sync = static_cast<uint8_t>(i % 2);
    }

    p.reverb.size = 0.75f;
    p.reverb.damping = 0.4f;
    p.reverb.width = 0.85f;
    p.reverb.mix = 0.3f;

    p.delay.time_s = 0.42f;
    p.delay.feedback = 0.55f;
    p.delay.mix = 0.4f;
    p.delay.stereo = 0.7f;
    p.delay.bpm_sync = 1;

    return p;
}

// Roundtrip tolerance per APVTS NormalisableRange skew. Skewed float params
// suffer a small precision loss when round-tripping through convertTo0to1 /
// convertFrom0to1. 0.5% is well below audible.
constexpr float kFloatRelTol = 5e-3f;
constexpr float kFloatAbsTol = 1e-3f;

void requireFloatsClose(float a, float b) {
    INFO("a=" << a << " b=" << b);
    REQUIRE_THAT(a, Catch::Matchers::WithinRel(b, kFloatRelTol) || Catch::Matchers::WithinAbs(b, kFloatAbsTol));
}

void expectPatchRoundtrip(const agentic_synth::PatchStruct& src, const agentic_synth::PatchStruct& dst) {
    using namespace agentic_synth;

    requireFloatsClose(dst.master_gain, src.master_gain);
    requireFloatsClose(dst.portamento_s, src.portamento_s);
    CHECK(dst.voice_count == src.voice_count);

    CHECK(dst.filter.type == src.filter.type);
    requireFloatsClose(dst.filter.cutoff_hz, src.filter.cutoff_hz);
    requireFloatsClose(dst.filter.resonance, src.filter.resonance);
    requireFloatsClose(dst.filter.env_mod, src.filter.env_mod);
    requireFloatsClose(dst.filter.key_track, src.filter.key_track);
    requireFloatsClose(dst.filter.drive, src.filter.drive);

    requireFloatsClose(dst.amp_env.attack_s, src.amp_env.attack_s);
    requireFloatsClose(dst.amp_env.decay_s, src.amp_env.decay_s);
    requireFloatsClose(dst.amp_env.sustain, src.amp_env.sustain);
    requireFloatsClose(dst.amp_env.release_s, src.amp_env.release_s);

    requireFloatsClose(dst.filter_env.attack_s, src.filter_env.attack_s);
    requireFloatsClose(dst.filter_env.decay_s, src.filter_env.decay_s);
    requireFloatsClose(dst.filter_env.sustain, src.filter_env.sustain);
    requireFloatsClose(dst.filter_env.release_s, src.filter_env.release_s);

    for (int i = 0; i < kMaxOscillators; ++i) {
        const auto& a = src.osc[i];
        const auto& b = dst.osc[i];
        CHECK(b.type == a.type);
        requireFloatsClose(b.detune_cents, a.detune_cents);
        requireFloatsClose(b.semitone_offset, a.semitone_offset);
        requireFloatsClose(b.wavetable_pos, a.wavetable_pos);
        requireFloatsClose(b.fm_ratio, a.fm_ratio);
        requireFloatsClose(b.fm_depth, a.fm_depth);
        requireFloatsClose(b.pulse_width, a.pulse_width);
        requireFloatsClose(b.pan, a.pan);
        requireFloatsClose(b.volume, a.volume);
        CHECK(b.enabled == a.enabled);
    }

    for (int i = 0; i < kMaxLfos; ++i) {
        const auto& a = src.lfo[i];
        const auto& b = dst.lfo[i];
        CHECK(b.waveform == a.waveform);
        CHECK(b.target == a.target);
        requireFloatsClose(b.rate_hz, a.rate_hz);
        requireFloatsClose(b.depth, a.depth);
        requireFloatsClose(b.phase_offset, a.phase_offset);
        CHECK(b.bpm_sync == a.bpm_sync);
    }

    requireFloatsClose(dst.reverb.size, src.reverb.size);
    requireFloatsClose(dst.reverb.damping, src.reverb.damping);
    requireFloatsClose(dst.reverb.width, src.reverb.width);
    requireFloatsClose(dst.reverb.mix, src.reverb.mix);

    requireFloatsClose(dst.delay.time_s, src.delay.time_s);
    requireFloatsClose(dst.delay.feedback, src.delay.feedback);
    requireFloatsClose(dst.delay.mix, src.delay.mix);
    requireFloatsClose(dst.delay.stereo, src.delay.stereo);
    CHECK(dst.delay.bpm_sync == src.delay.bpm_sync);
}

} // namespace

TEST_CASE("APVTS roundtrip: writePatchToApvts → buildPatchFromApvts is identity (within float tolerance)") {
    // The two helpers must be mutual inverses across every PatchStruct field.
    // If they aren't, AI patches → APVTS → engine + UI knobs ←→ APVTS → engine
    // disagree, and state recall silently corrupts the patch.
    PluginFixture fix;
    AgenticSynthPlugin plug;
    plug.prepareToPlay(44100.0, 512);

    const auto src = makeRichTestPatch();
    plug.writePatchToApvtsForTest(src);
    const auto roundtripped = plug.buildPatchFromApvtsForTest();
    expectPatchRoundtrip(src, roundtripped);

    plug.agentBridge().setMidiNoteSink(nullptr);
}

TEST_CASE("APVTS state recall: getStateInformation / setStateInformation preserves every PatchStruct field") {
    // The Phase 1 lifecycle suite covered only filterCutoff / filterResonance /
    // masterGain. Phase 2 has to prove every field survives an XML round-trip.
    PluginFixture fix;
    AgenticSynthPlugin plugA;
    plugA.prepareToPlay(44100.0, 512);
    AgenticSynthPlugin plugB;
    plugB.prepareToPlay(44100.0, 512);

    const auto src = makeRichTestPatch();
    plugA.writePatchToApvtsForTest(src);

    juce::MemoryBlock state;
    plugA.getStateInformation(state);
    REQUIRE(state.getSize() > 0);

    plugB.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto reloaded = plugB.buildPatchFromApvtsForTest();
    expectPatchRoundtrip(src, reloaded);

    plugA.agentBridge().setMidiNoteSink(nullptr);
    plugB.agentBridge().setMidiNoteSink(nullptr);
}

TEST_CASE("AI patch → APVTS pump propagates to engine on next processBlock") {
    // Simulates the AgentBridge → message-thread Timer → APVTS → audio-thread
    // applyParameters chain. Pushes a patch through the AgentBridge SPSC pipe,
    // pumps the message-thread side synchronously, and verifies the engine
    // smoothers reflect the new values after one processBlock.
    PluginFixture fix;
    AgenticSynthPlugin plug;
    plug.prepareToPlay(44100.0, 512);

    // Inject an AI patch into the pre-patch pipeline (same path the LLM
    // streaming parser uses).
    agentic_synth::PatchStruct ai = agentic_synth::make_default_patch();
    ai.filter.cutoff_hz = 1234.0f;
    ai.filter.resonance = 0.42f;
    ai.master_gain = 0.33f;
    ai.amp_env.attack_s = 0.07f;

    plug.agentBridge().refinePatch(ai); // pushes onto the SPSC pipe

    // Pump synchronously (test harness equivalent of the juce::Timer tick).
    plug.pumpAgentBridgeForTest();

    // APVTS now reflects the AI values.
    const float postCutoff =
        plug.getAPVTS().getRawParameterValue("filterCutoff")->load();
    const float postGain =
        plug.getAPVTS().getRawParameterValue("masterGain")->load();
    CHECK_THAT(postCutoff, Catch::Matchers::WithinRel(1234.0f, 1e-3f));
    CHECK_THAT(postGain, Catch::Matchers::WithinRel(0.33f, 1e-3f));

    // Render enough samples for the 30 Hz cutoff smoother to converge
    // (τ ≈ 5 ms at 44.1 kHz → 4096 samples = ~17τ, > 99.99% convergence).
    // applyParameters runs at the head of every processBlock, so the smoother
    // target is locked in after the first block and the remainder of the
    // render just lets it glide there.
    (void)renderPluginPeak(plug, 4096);
    CHECK_THAT(plug.voiceManagerForTest().currentSmoothedCutoff(),
               Catch::Matchers::WithinAbs(1234.0f, 1.0f));

    plug.agentBridge().setMidiNoteSink(nullptr);
}

TEST_CASE("Host automation: setValueNotifyingHost into APVTS reaches engine after one block") {
    // Models a host driving an automation lane directly on the APVTS param.
    // The audio thread must pick the change up via applyParameters on the
    // next processBlock — no manual setter call required.
    PluginFixture fix;
    AgenticSynthPlugin plug;
    plug.prepareToPlay(44100.0, 512);

    auto* cutoffParam = plug.getAPVTS().getParameter("filterCutoff");
    REQUIRE(cutoffParam != nullptr);
    cutoffParam->setValueNotifyingHost(cutoffParam->convertTo0to1(440.0f));

    auto* gainParam = plug.getAPVTS().getParameter("masterGain");
    REQUIRE(gainParam != nullptr);
    gainParam->setValueNotifyingHost(gainParam->convertTo0to1(0.2f));

    // Same convergence rationale as the AI-pump test above — 30 Hz smoother
    // needs ~17τ ≈ 4096 samples at 44.1 kHz to settle to within 1 Hz / 0.001
    // gain of the target.
    (void)renderPluginPeak(plug, 4096);
    CHECK_THAT(plug.voiceManagerForTest().currentSmoothedCutoff(),
               Catch::Matchers::WithinAbs(440.0f, 1.0f));
    CHECK_THAT(plug.voiceManagerForTest().currentSmoothedGain(),
               Catch::Matchers::WithinAbs(0.2f, 1e-3f));

    plug.agentBridge().setMidiNoteSink(nullptr);
}

// ── Phase 2 follow-up tests ──────────────────────────────────────────────────
//
// 2. Real juce::Timer end-to-end: prove startTimer(20) is alive — pushing a
//    patch and running the dispatch loop must reach APVTS without any manual
//    pumpAgentBridgeForTest call.
// 3. Timer lifecycle race: construct plugin, push patch, destruct
//    immediately. Verify clean shutdown with pending work (no UAF / crash).
// 4. Exhaustive enum round-trip across every choice param.
// 5. Boundary round-trip for a representative set of critical float params.

#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

TEST_CASE("Phase-2 follow-up: real juce::Timer pumps AgentBridge → APVTS end-to-end") {
    // No manual pump — proves AgenticSynthPlugin::startTimer(kAgentBridgePollIntervalMs)
    // actually wired the Timer to MessageManager. If startTimer were deleted
    // this test would assert on stale APVTS values.
    PluginFixture fix;
    AgenticSynthPlugin plug;
    plug.prepareToPlay(44100.0, 512);

    agentic_synth::PatchStruct ai = agentic_synth::make_default_patch();
    ai.filter.cutoff_hz = 777.0f;
    ai.master_gain = 0.21f;
    plug.agentBridge().refinePatch(ai);

    // Plugin Timer fires every 20 ms — runDispatchLoopUntil(80) gives ~4
    // firings of headroom plus a 20 ms PrePatch interpolation cycle.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(80);

    const float postCutoff = plug.getAPVTS().getRawParameterValue("filterCutoff")->load();
    const float postGain = plug.getAPVTS().getRawParameterValue("masterGain")->load();
    CHECK_THAT(postCutoff, Catch::Matchers::WithinRel(777.0f, 1e-3f));
    CHECK_THAT(postGain, Catch::Matchers::WithinRel(0.21f, 1e-3f));

    plug.agentBridge().setMidiNoteSink(nullptr);
}

TEST_CASE("Phase-2 follow-up: plugin destructs cleanly with pending Timer work") {
    // Push a patch and immediately destruct without pumping. The Timer must
    // be stopped in ~AgenticSynthPlugin so no callback fires into the
    // partially-destroyed object. Run with ASAN/UBSAN to catch UAF if the
    // destruction order regresses.
    PluginFixture fix;
    {
        AgenticSynthPlugin plug;
        plug.prepareToPlay(44100.0, 256);
        agentic_synth::PatchStruct ai = agentic_synth::make_default_patch();
        ai.filter.cutoff_hz = 2222.0f;
        plug.agentBridge().refinePatch(ai);
        plug.agentBridge().setMidiNoteSink(nullptr);
        // Out-of-scope here: dtor must stopTimer before AgentBridge / APVTS
        // go away. No crash, no UAF.
    }
    // Pump residual messages — anything left over must not touch the freed
    // plugin (juce::Timer guarantees stopTimer awaits in-flight ticks).
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    SUCCEED("No crash on destruct-with-pending-Timer-work");
}

TEST_CASE("Phase-2 follow-up: exhaustive enum round-trip for every choice param") {
    // For every enum-backed param + every valid index: set via APVTS →
    // buildPatchFromApvts → recovered enum equals what we set. Catches any
    // off-by-one in safeEnumIndex or the choice-param ↔ enum mapping table.
    PluginFixture fix;
    AgenticSynthPlugin plug;
    plug.prepareToPlay(44100.0, 256);

    using namespace agentic_synth;

    auto setChoiceIndex = [&](const juce::String& id, int idx, int count) {
        auto* p = plug.getAPVTS().getParameter(id);
        REQUIRE(p != nullptr);
        // Normalised value for choice index i with N options is i / (N-1).
        const float norm = (count > 1) ? static_cast<float>(idx) / static_cast<float>(count - 1) : 0.0f;
        p->setValueNotifyingHost(norm);
    };

    // ── OscType (8 values) on osc0 ───────────────────────────────────────
    SECTION("OscType round-trip on osc0") {
        const int oscTypeCount = 8;
        const int idx = GENERATE(range(0, 8));
        setChoiceIndex("osc0_type", idx, oscTypeCount);
        const auto patch = plug.buildPatchFromApvtsForTest();
        CHECK(static_cast<int>(patch.osc[0].type) == idx);
    }
    // ── FilterType (5 values) ────────────────────────────────────────────
    SECTION("FilterType round-trip") {
        const int filterTypeCount = 5;
        const int idx = GENERATE(range(0, 5));
        setChoiceIndex("filter_type", idx, filterTypeCount);
        const auto patch = plug.buildPatchFromApvtsForTest();
        CHECK(static_cast<int>(patch.filter.type) == idx);
    }
    // ── LfoWaveform (5 values) on lfo0 ───────────────────────────────────
    SECTION("LfoWaveform round-trip on lfo0") {
        const int waveformCount = 5;
        const int idx = GENERATE(range(0, 5));
        setChoiceIndex("lfo0_waveform", idx, waveformCount);
        const auto patch = plug.buildPatchFromApvtsForTest();
        CHECK(static_cast<int>(patch.lfo[0].waveform) == idx);
    }
    // ── LfoTarget (7 values) on lfo0 ─────────────────────────────────────
    SECTION("LfoTarget round-trip on lfo0") {
        const int targetCount = 7;
        const int idx = GENERATE(range(0, 7));
        setChoiceIndex("lfo0_target", idx, targetCount);
        const auto patch = plug.buildPatchFromApvtsForTest();
        CHECK(static_cast<int>(patch.lfo[0].target) == idx);
    }

    plug.agentBridge().setMidiNoteSink(nullptr);
}

TEST_CASE("Phase-4 follow-up: cross-cutting LFO+filter-crossfade+wavetable integration is finite & smooth",
          "[phase4-followup][integration]") {
    // Smoke test that exercises three modulation paths simultaneously, then
    // mutates the patch twice mid-render to trigger filter-type crossfade +
    // a wavetable-pos LFO retarget. All samples must remain finite, bounded,
    // and sample-to-sample-bounded — no NaN, no blow-up, no click.
    using namespace agentic_synth;
    PluginFixture fix;
    AgenticSynthPlugin plug;
    plug.prepareToPlay(44100.0, 1024);

    // Build the initial patch: sine osc, LowPass filter, LFO0 → Amplitude
    // depth 0.8, everything else neutral.
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 6000.0f;
    p.filter.resonance = 0.3f;
    p.filter.type = FilterType::LowPass;
    p.osc[0].enabled = 1;
    p.osc[0].type = OscType::Wavetable;
    p.osc[0].volume = 0.6f;
    p.osc[0].wavetable_pos = 0.25f;
    p.osc[1].enabled = 0;
    p.osc[2].enabled = 0;
    p.amp_env.attack_s = 0.001f;
    p.amp_env.decay_s = 0.001f;
    p.amp_env.sustain = 1.0f;
    p.amp_env.release_s = 0.5f;
    p.lfo[0].waveform = LfoWaveform::Sine;
    p.lfo[0].target = LfoTarget::Amplitude;
    p.lfo[0].rate_hz = 4.0f;
    p.lfo[0].depth = 0.8f;
    p.lfo[0].bpm_sync = 0;
    // LFO1 initially off — we activate it on the second applyPatch.
    p.lfo[1].depth = 0.0f;
    p.lfo[1].target = LfoTarget::None;

    plug.writePatchToApvtsForTest(p);

    auto& q = plug.auditionQueueForTest();
    REQUIRE(q.push(RawMidiMsg::noteOn(60, 100)));

    // Block 1: 1024 samples — let the voice + LFO settle.
    auto renderBlock = [&](int n) {
        juce::AudioBuffer<float> buf(2, n);
        buf.clear();
        juce::MidiBuffer midi;
        plug.processBlock(buf, midi);
        std::vector<float> all;
        all.reserve(static_cast<std::size_t>(2 * n));
        for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
            const auto* d = buf.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
                all.push_back(d[i]);
        }
        return all;
    };

    const auto first = renderBlock(1024);

    // Block 2: swap filter type LowPass → HighPass → triggers crossfade.
    p.filter.type = FilterType::HighPass;
    plug.writePatchToApvtsForTest(p);
    const auto second = renderBlock(1024);

    // Block 3: bump wavetable_pos and turn on LFO1 → WavetablePos depth 0.5.
    p.osc[0].wavetable_pos = 0.75f;
    p.lfo[1].waveform = LfoWaveform::Sine;
    p.lfo[1].target = LfoTarget::WavetablePos;
    p.lfo[1].rate_hz = 2.0f;
    p.lfo[1].depth = 0.5f;
    p.lfo[1].bpm_sync = 0;
    plug.writePatchToApvtsForTest(p);
    const auto third = renderBlock(1024);

    auto check = [](const std::vector<float>& buf, const char* tag) {
        REQUIRE(!buf.empty());
        float prev = buf.front();
        float maxAbs = 0.0f;
        float maxStep = 0.0f;
        for (std::size_t i = 0; i < buf.size(); ++i) {
            const float s = buf[i];
            INFO(tag << " sample " << i);
            REQUIRE(std::isfinite(s));
            maxAbs = std::max(maxAbs, std::abs(s));
            if (i > 0)
                maxStep = std::max(maxStep, std::abs(s - prev));
            prev = s;
        }
        INFO(tag << " maxAbs=" << maxAbs << " maxStep=" << maxStep);
        REQUIRE(maxAbs < 2.0f);
        REQUIRE(maxStep < 0.5f);
    };

    check(first, "first");
    check(second, "second");
    check(third, "third");

    plug.agentBridge().setMidiNoteSink(nullptr);
}

TEST_CASE("Phase-2 follow-up: boundary round-trip for critical AudioParameterFloat") {
    // Set selected critical float params to their NormalisableRange min and
    // max, round-trip via getStateInformation / setStateInformation, and
    // assert the recovered value is within tight tolerance. Skewed ranges
    // pay a small precision cost on convertTo0to1/convertFrom0to1 so we
    // accept 0.1% rel or 1e-4 abs.
    PluginFixture fix;
    AgenticSynthPlugin plugA;
    plugA.prepareToPlay(44100.0, 256);
    AgenticSynthPlugin plugB;
    plugB.prepareToPlay(44100.0, 256);

    struct Probe {
        const char* id;
        float minV;
        float maxV;
    };
    // Picked deliberately: a couple of skewed ranges (cutoff, attack, release),
    // a linear one (resonance), and the delay time edge.
    const auto probe = GENERATE(values<Probe>({
        {"filterCutoff", 20.0f, 20000.0f},
        {"filterResonance", 0.0f, 1.0f},
        {"ampAttack", 0.001f, 10.0f},
        {"ampRelease", 0.001f, 20.0f},
        {"delay_time", 0.001f, 2.0f},
        {"lfo0_rate_hz", 0.01f, 20.0f},
    }));

    const bool isMin = GENERATE(true, false);
    const float target = isMin ? probe.minV : probe.maxV;

    auto* paramA = plugA.getAPVTS().getParameter(probe.id);
    REQUIRE(paramA != nullptr);
    paramA->setValueNotifyingHost(paramA->convertTo0to1(target));

    juce::MemoryBlock state;
    plugA.getStateInformation(state);
    REQUIRE(state.getSize() > 0);
    plugB.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    auto* paramB = plugB.getAPVTS().getParameter(probe.id);
    REQUIRE(paramB != nullptr);
    const float recovered = paramB->convertFrom0to1(paramB->getValue());

    INFO("param=" << probe.id << " target=" << target << " recovered=" << recovered);
    CHECK_THAT(recovered, Catch::Matchers::WithinRel(target, 1e-3f) ||
                              Catch::Matchers::WithinAbs(target, 1e-4f));

    plugA.agentBridge().setMidiNoteSink(nullptr);
    plugB.agentBridge().setMidiNoteSink(nullptr);
}


// Phase 5: direct tests for Phase 2 follow-up additions. Previously only
// indirect coverage existed (host channel-layout probes, bounce-tail timing).
// Direct unit assertions prevent silent regressions.

TEST_CASE("getTailLengthSeconds returns 5 seconds for reverb+delay tail",
          "[plugin][lifecycle][tail]") {
    AgenticSynthPlugin p;
    const double tail = p.getTailLengthSeconds();
    REQUIRE_THAT(tail, Catch::Matchers::WithinAbs(5.0, 1e-9));
}

TEST_CASE("isBusesLayoutSupported accepts mono and stereo main output",
          "[plugin][lifecycle][buses]") {
    AgenticSynthPlugin p;

    juce::AudioProcessor::BusesLayout stereo;
    stereo.outputBuses.add(juce::AudioChannelSet::stereo());
    CHECK(p.isBusesLayoutSupported(stereo));

    juce::AudioProcessor::BusesLayout mono;
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    CHECK(p.isBusesLayoutSupported(mono));
}

TEST_CASE("isBusesLayoutSupported rejects unsupported layouts",
          "[plugin][lifecycle][buses]") {
    AgenticSynthPlugin p;

    juce::AudioProcessor::BusesLayout fiveOne;
    fiveOne.outputBuses.add(juce::AudioChannelSet::create5point1());
    CHECK_FALSE(p.isBusesLayoutSupported(fiveOne));

    juce::AudioProcessor::BusesLayout quad;
    quad.outputBuses.add(juce::AudioChannelSet::quadraphonic());
    CHECK_FALSE(p.isBusesLayoutSupported(quad));

    juce::AudioProcessor::BusesLayout disabled;
    disabled.outputBuses.add(juce::AudioChannelSet::disabled());
    CHECK_FALSE(p.isBusesLayoutSupported(disabled));
}
